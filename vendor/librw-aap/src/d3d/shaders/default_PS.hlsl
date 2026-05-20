// Default pixel shader.
//
// Variants compiled from this file:
//   default_PS              - legacy LDR (no PP, no GBUFFER)
//   default_tex_PS          - legacy LDR with diffuse texture
//   default_pp_PS / _tex_PS - per-pixel lighting, LDR (single RT)
//   default_pp_gbuf_PS / _tex_PS - per-pixel lighting, HDR-friendly, writes MRT:
//                                  COLOR0 = scene (HDR linear), COLOR1 = packed
//                                  world-normal (rgb) + linear depth (a)

#ifdef PER_PIXEL_LIGHTING
#include "standardConstantsPS.h"
#endif

struct VS_out {
	float4 Position		: POSITION;
	float3 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
#ifdef PER_PIXEL_LIGHTING
	float3 WorldNormal	: TEXCOORD1;
	float3 WorldPos		: TEXCOORD2;
#ifdef GBUFFER
	float  ViewDepth	: TEXCOORD3;
#endif
#endif
};

sampler2D tex0 : register(s0);

float4 fogColor : register(c0);

#ifdef PER_PIXEL_LIGHTING
// .xyz = world-space camera position
// .w   = Blinn-Phong specular power (32 by default, tweaked by host)
float4 eyePosPS : register(c42);

// Procedural IBL — sky/horizon/ground gradient sampled by world-space normal.
// Host uploads these once per scene render from CTimeCycle (sky colours +
// derived horizon mix). Intensity = 0 → zero contribution, so the per-frame
// upload doesn't cost anything when IBL is off.
//
// .rgb = colour, .a unused (kept for c-register alignment).
float4 iblSky      : register(c44);	// hemisphere zenith (N.z = +1)
float4 iblHorizon  : register(c45);	// hemisphere ring (|N.z| ≈ 0)
float4 iblGround   : register(c46);	// hemisphere nadir (N.z = -1, muted)
// .x = intensity multiplier (0 = off)
// .y = horizon falloff exponent (1.0 = linear, larger = sharper horizon band)
// .z = use-cube switch (1.0 = sample irradianceCube on s7 instead of the gradient)
// .w = unused
float4 iblParams   : register(c47);

// Phase 2 irradiance cubemap (bound by CIBL::BindReceiver on s7). When
// iblParams.z > 0.5 we sample this instead of the gradient: a single
// texCUBE replaces the 3-term hemisphere split.
samplerCUBE iblIrradianceCube : register(s7);

// Specular reflection cube — same handle as the IBL capture cube,
// sampled along the world-space reflection vector to give Fresnel-
// weighted reflections on every shaded surface (buildings, road,
// ground, peds — not just cars). When the host hasn't bound this cube
// (iblParams.z = 0) the sample comes from the same s7 / irradiance
// path and the contribution multiplies out via reflProps below.
samplerCUBE iblReflectionCube : register(s8);

// Per-pixel specular reflection weight. .x = strength (0 = off).
// The pipeline still drives the lit/spec terms; this is a Fresnel-
// weighted *additive* contribution on top of the colour, capturing
// "this surface reflects the sky toward the camera". c64+ lives above
// the CSM block (c48..c63) so we don't collide with the cascade matrices.
float4 iblReflParams : register(c64);

// Wet-surface modulation. Driven by CWeather::WetRoads + Rain. Up-facing
// surfaces (puddles, car roofs, road tops) get the heaviest hit:
//   - diffuse darkens (wet asphalt absorbs more light)
//   - specular intensifies and tightens (water sheen)
// .x = wetness (0 = dry, 1 = soaked)
// .y = diffuse darkening factor (typical 0.5 → wet surface ½ as bright diffuse)
// .z = specular boost (typical 2.0..4.0)
// .w = specular power multiplier (typical 2.0 → tighter highlight)
float4 wetnessParams : register(c63);

// Rain ripples — procedural noise-based normal perturbation on wet
// up-facing surfaces. Host uploads via setRainRipples + uploadIBL.
//   .x = accumulated rain time (seconds, drives sin phase)
//   .y = strength (0 = bypass, 1 = full); host fades with CWeather::Rain
//   .z = world-XY tile scale (smaller → larger pattern; ~0.5 typical)
//   .w = reserved
float4 rainRipplesParams : register(c65);

// Wet puddles — spatial variation on the existing wetMask so that some
// patches read as puddle spots (more reflective, deeper-looking) while
// the surrounding ground reads as merely damp. Driven by a cheap 2-tap
// world-XY noise function — no extra texture, no extra RT. Host
// uploads via setPuddles + uploadIBL.
//   .x = strength (0 = bypass, 1 = full puddle boost on top of wetness)
//   .y = tile scale (smaller → larger puddle pattern; ~0.08 typical
//        ≈ ~12m puddle period — feels plausible for street drainage)
//   .z = darken multiplier (puddle spots darken diffuse this much further
//        beyond the base wetnessParams.y; ~0.5..0.8 typical)
//   .w = reserved
float4 puddlesParams : register(c66);

// Cheap procedural puddle mask. 2 sin terms at different scales — not
// "real" noise but the eye reads it as a natural splotchy pattern on
// the ground. Returns 0..1 puddle factor (0 = dry patch, 1 = full
// puddle). Used by the wetness path to boost the local wet effect.
float PuddleMask(float2 worldXY, float tileScale)
{
	float2 p = worldXY * tileScale;
	// Octave 1 — large-scale splotches.
	float a = sin(p.x * 0.9 + 1.7) * sin(p.y * 1.1 - 0.3);
	// Octave 2 — small-scale detail breaks up the regular pattern.
	float b = sin(p.x * 2.3 - 0.4) * sin(p.y * 2.7 + 1.1) * 0.4;
	// Reshape to a roughly bell-shaped distribution so isolated bright
	// spots become the actual puddles, not a 50/50 checker.
	float n = (a + b) * 0.5 + 0.5;	// 0..1
	return saturate(n * n * 2.0 - 0.8);	// peaks ~0..1, zero outside
}

// Procedural rain ripple normal perturbation. Two-octave sin pattern on
// world XY, animated by time. Cheap (~8 ALU + 4 sin) vs sampling a
// tiled normal-map texture; no extra TXD asset required. Returns the
// tangent-space xy perturbation; the caller adds it to N.xy and renorms.
float2 ComputeRainRippleN(float2 worldXY, float time, float strength, float tileScale)
{
	if(strength < 0.01)
		return float2(0, 0);
	float2 p = worldXY * tileScale;
	float t = time * 2.5;
	// Two octaves at different angles + speeds so the pattern doesn't
	// look like a grid. Amplitude small (0.04) — we want a *subtle*
	// shimmer, not a wave pool. Strength dial scales linearly on top.
	float2 n;
	n.x = sin(p.x * 1.0 + t)          + sin(p.y * 1.3 + t * 1.1) * 0.6;
	n.y = cos(p.x * 1.1 + t * 0.9)    + cos(p.y * 0.9 + t * 1.0) * 0.7;
	return n * strength * 0.04;
}

// Dynamic point lights — independent of librw's per-atomic lights[]
// array. Host (CDynamicLights) picks the top-N brightest CPointLights
// near the camera each frame and uploads them here once per scene.
// EVERY pp_PS atomic samples this array, so buildings + props + peds
// (which weren't fed by GenerateLightsAffectingObject in re3) finally
// get illuminated by car headlights, lamp posts, gunshots, explosions.
//
// dynLightCount.x = active light count (0..32). When 0 the [loop]
// short-circuits and pays nothing.
//
// Each light is two vec4s: position+radius and color+intensity.
// 32 lights × 2 vec4 = 64 registers at c101..c164 — fits ps_3_0's
// 224-float-constant budget alongside CSM (c48..c62), spot shadow
// (c70..c75), and the wetness / IBL constants.
float4 dynLightCount : register(c100);
float4 dynLightData[64] : register(c101);

// Spot shadow receiver — CSpotShadow picks the brightest CPointLights
// LIGHT_POINT each frame, renders a depth-only pass from its position
// looking down, and binds the result here. The receiver projects the
// pixel's world position into the light's clip space, compares depth
// to the shadow map sample, and modulates slot-0 of the dyn light
// array by the resulting visibility.
//
// Slot 0 is the same light CSpotShadow picked (both rank by luminance
// × inverse-distance²). When CSpotShadow disables itself (no valid
// light, GPU lacks R32F, …), tuning.x = 0 and the lerp(1, shadow,0)
// reduces to fully lit.
sampler2D spotShadowTex : register(s9);
float4x4  spotShadowMat : register(c70);
float4    spotShadowLightPos : register(c74);	// xyz pos, w radius
float4    spotShadowTune : register(c75);	// .x = strength, .y = invMapSize, .z = bias, .w = softness

// Local visibility helper — avoids the deprecated `step()` intrinsic
// path which HLSL flags on some configurations with non-constant args.
float SpotShadow_Visible(float ref, float sampled) { return sampled >= ref ? 1.0 : 0.0; }

float SampleSpotShadow(float3 worldPos)
{
	float4 lp = mul(spotShadowMat, float4(worldPos, 1.0));
	if(lp.w <= 0.0001) return 1.0;
	lp.xyz /= lp.w;
	float2 uv = lp.xy * 0.5 + 0.5;
	uv.y = 1.0 - uv.y;
	float refZ = lp.z - spotShadowTune.z;
	// Out-of-frustum → fully lit. (Pixel is outside the spot light's
	// cone, so it doesn't get any of its contribution anyway.)
	if(any(uv < 0.0) || any(uv > 1.0) || refZ > 1.0 || refZ < 0.0)
		return 1.0;
	// 4-tap unit cross PCF — soft, cheap, no Poisson noise pattern.
	float2 stp = spotShadowTune.yy * max(spotShadowTune.w, 0.5);
	float vis = 0.0;
	vis += SpotShadow_Visible(refZ, tex2D(spotShadowTex, uv + float2( stp.x, 0)).r);
	vis += SpotShadow_Visible(refZ, tex2D(spotShadowTex, uv + float2(-stp.x, 0)).r);
	vis += SpotShadow_Visible(refZ, tex2D(spotShadowTex, uv + float2(0,  stp.y)).r);
	vis += SpotShadow_Visible(refZ, tex2D(spotShadowTex, uv + float2(0, -stp.y)).r);
	return vis * 0.25;
}

// Dynamic point light receiver — applies the host-uploaded N nearest
// CPointLights to the per-pixel surface. Walks dynLightData[i*2 + 0/1]
// (pos+radius, colour+intensity), masks by inverse-square × radius
// cutoff × NdotL.
//
// IMPORTANT: this is the "with spot shadow" variant. The slot-0 light is
// the same one CSpotShadow rendered its depth map for, so we sample that
// depth map to suppress contribution in shadowed pixels. Slots 1..N-1
// get full contribution since we have no per-light shadow maps.
//
// A SIMPLER COPY without the spot shadow lives in
// `src/extras/shaders/neoVehicle_PS.hlsl` (vehicle bodies don't go
// through this pp_PS path — they have their own pipeline). Keep the
// two in sync for the inverse-square + radius-cutoff math.
float3 ApplyDynamicPointLights(float3 worldPos, float3 N)
{
	float3 sum = float3(0, 0, 0);
	int n = (int)dynLightCount.x;
	// Spot shadow visibility for slot 0 (the brightest light). Cheap
	// `if` skips the texture sample + projection when CSpotShadow is
	// off (tune.x = 0).
	float spotVis = 1.0;
	if(spotShadowTune.x > 0.001)
		spotVis = lerp(1.0, SampleSpotShadow(worldPos), saturate(spotShadowTune.x));
	[loop]
	for(int i = 0; i < n; i++){
		float4 lp  = dynLightData[i*2 + 0];	// xyz = world pos, w = radius
		float4 lc  = dynLightData[i*2 + 1];	// rgb = colour, w = intensity
		float3 toL = lp.xyz - worldPos;
		float distSq = dot(toL, toL);
		float radius = lp.w;
		// Sphere cutoff — out-of-range lights add nothing. Branch is
		// per-pixel + per-light so cheap on modern GPUs.
		if(distSq > radius * radius)
			continue;
		float dist = sqrt(max(distSq, 1e-6));
		float3 L = toL / dist;
		float ndotl = saturate(dot(N, L));
		// Inverse-square attenuation with smooth window cutoff at
		// `radius`. Standard formula: (1 - d/r)² × falloff.
		float t = 1.0 - dist / radius;
		float atten = t * t;
		// Apply spot shadow only to the brightest light (slot 0) —
		// matches what CSpotShadow rendered its depth map for. Other
		// slots get full contribution because we have no shadow map
		// for them.
		float lightShadow = (i == 0) ? spotVis : 1.0;
		sum += lc.rgb * lc.w * ndotl * atten * lightShadow;
	}
	return sum;
}

// CSM receiver — 3 cascades worth of light-view-proj matrices + split
// distances, plus a per-cascade depth sampler. Disabled when csmParams.w
// (overall strength) is 0; uploadCSM() forces it there when the host
// hasn't enabled the cascade renderer, so the [branch] short-circuits.
//
// Matrix layout: row-major 4×4, one per cascade (3 cascades × 4 c-regs
// = c48..c59). Each transforms a world-space position into the cascade's
// orthographic clip space; UV = clip.xy*0.5+0.5, refDepth = clip.z.
float4x4 csmLightViewProj0 : register(c48);
float4x4 csmLightViewProj1 : register(c52);
float4x4 csmLightViewProj2 : register(c56);

// .xyz = view-space depth thresholds for cascade 0/1/2 boundaries
// .w   = overall shadow strength (0 = bypass, 1 = full)
float4 csmParams : register(c60);

// .xy = 1/MapSize (PCF texel step), .z = depth bias, .w = blend overlap (m)
float4 csmTuning : register(c61);

sampler2D csmTex0 : register(s4);
sampler2D csmTex1 : register(s5);
sampler2D csmTex2 : register(s6);

// 4-tap Poisson disc — small kernel, sub-pixel offsets. Used when the
// host requests low-quality CSM PCF (csmTuning2.x = 0).
static const float2 csmPoisson4[4] = {
	float2( 0.94558,  0.76995),
	float2(-0.81544,  0.18687),
	float2(-0.20254, -0.86342),
	float2( 0.51842, -0.40664),
};

// 16-tap Poisson disc (Mitchell-style distribution) — much softer + less
// banding at the cost of 4× the texture reads. Used when csmTuning2.x ∈ (0.5,1.5)
// (Soft mode).
static const float2 csmPoisson16[16] = {
	float2( 0.94558,  0.76995),
	float2(-0.81544,  0.18687),
	float2(-0.20254, -0.86342),
	float2( 0.51842, -0.40664),
	float2( 0.31334,  0.92577),
	float2(-0.55781,  0.65728),
	float2(-0.92345, -0.32018),
	float2(-0.42124, -0.10918),
	float2( 0.04123, -0.51284),
	float2( 0.79827,  0.10342),
	float2(-0.13567,  0.34128),
	float2( 0.61283,  0.43712),
	float2(-0.30852, -0.59123),
	float2(-0.71042,  0.36018),
	float2( 0.18267, -0.18412),
	float2( 0.45123,  0.05731),
};

// 32-tap Poisson disc — used when csmTuning2.x > 1.5 (Ultra mode).
// Doubles sample count over Soft, halving visible PCF banding at the cost
// of 2× the texture reads. The extra 16 points are interleaved (golden-
// angle spiral seeded off the first 16) so the kernel stays well-stratified
// at the larger radii used for very soft shadows.
static const float2 csmPoisson32[32] = {
	float2( 0.94558,  0.76995),
	float2(-0.81544,  0.18687),
	float2(-0.20254, -0.86342),
	float2( 0.51842, -0.40664),
	float2( 0.31334,  0.92577),
	float2(-0.55781,  0.65728),
	float2(-0.92345, -0.32018),
	float2(-0.42124, -0.10918),
	float2( 0.04123, -0.51284),
	float2( 0.79827,  0.10342),
	float2(-0.13567,  0.34128),
	float2( 0.61283,  0.43712),
	float2(-0.30852, -0.59123),
	float2(-0.71042,  0.36018),
	float2( 0.18267, -0.18412),
	float2( 0.45123,  0.05731),
	// Interleaved fill-ins.
	float2( 0.13427,  0.62874),
	float2(-0.39871,  0.83142),
	float2(-0.10248,  0.05317),
	float2( 0.27843, -0.74521),
	float2(-0.62318, -0.51743),
	float2( 0.83291, -0.28415),
	float2( 0.69853, -0.61247),
	float2(-0.06281, -0.27154),
	float2(-0.50912,  0.21478),
	float2( 0.37418,  0.55731),
	float2(-0.84613,  0.55817),
	float2( 0.61374,  0.81824),
	float2(-0.27184, -0.41218),
	float2( 0.05721,  0.81234),
	float2(-0.62418, -0.18421),
	float2( 0.21487, -0.21587),
};

// Additional tuning slot (csmTuning is c61, this lives at c62).
// .x = filter mode
//      0 = Sharp (4-tap PCF)
//      1 = Soft  (16-tap PCF)
//      2 = Ultra (32-tap PCF)
//      3 = VSM   (Variance Shadow Maps via Chebyshev — naturally smooth)
// .y = PCF radius multiplier (1.0 = stock, 2..3 = even softer)
// .z, .w = reserved
float4 csmTuning2 : register(c62);

float CSMSampleCascade(int idx, float3 worldPos)
{
	float4 lp;
	if(idx == 0)      lp = mul(float4(worldPos, 1), csmLightViewProj0);
	else if(idx == 1) lp = mul(float4(worldPos, 1), csmLightViewProj1);
	else              lp = mul(float4(worldPos, 1), csmLightViewProj2);
	lp.xyz /= max(lp.w, 1e-4);
	float2 uv = lp.xy * 0.5 + 0.5;
	uv.y = 1.0 - uv.y;
	float refZ = lp.z - csmTuning.z;	// biased ref depth

	// Off the cascade map = fully lit
	if(any(uv < 0) || any(uv > 1) || refZ > 1 || refZ < 0)
		return 1.0;

	float vis = 0.0;
	float2 texelStep = csmTuning.xy * max(csmTuning2.y, 1.0);
	// Four-mode shadow filter — Sharp / Soft / Ultra (PCF) + VSM. Branches
	// are uniform (csmTuning2.x is a static constant for the whole frame),
	// so the compiler keeps only one branch alive per draw. VSM trades the
	// progressively-larger PCF sample count for one .rg fetch + a closed-
	// form Chebyshev inequality — naturally smooth shadows that handle
	// soft penumbras without the PCF banding, at the cost of "light bleed"
	// on tightly nested occluders.
	if(csmTuning2.x > 2.5){
		// VSM — sample (depth, depth²) once per cascade. Chebyshev
		// inequality: P(z > t) ≤ σ² / (σ² + (μ - t)²). Returns 1 when
		// fully lit, <1 in penumbra, 0 in full shadow. Numerical guard
		// keeps σ² ≥ 1e-5 so the divide is finite on flat occluders.
		float2 m;
		if(idx == 0)      m = tex2D(csmTex0, uv).rg;
		else if(idx == 1) m = tex2D(csmTex1, uv).rg;
		else              m = tex2D(csmTex2, uv).rg;
		float mu = m.x;
		float variance = max(m.y - mu * mu, 1e-5);
		float d = refZ - mu;
		// Lit: t below the mean → fully lit; otherwise probabilistic.
		if(d <= 0.0)
			return 1.0;
		float pmax = variance / (variance + d * d);
		// Light-bleeding-reduction — sharpen the falloff so partial-shadow
		// pixels don't bleed light through thin occluders. Linear chop
		// at 0.2 is a standard trick (Donnelly & Lauritzen 2006).
		return saturate((pmax - 0.2) / 0.8);
	}else if(csmTuning2.x > 1.5){
		// Ultra — 32 taps. Halves visible PCF banding at the cost of 2×
		// the work of Soft. Recommended at 1080p+ on modern GPUs.
		[unroll]
		for(int i = 0; i < 32; i++){
			float2 ofs = csmPoisson32[i] * texelStep;
			float d;
			if(idx == 0)      d = tex2D(csmTex0, uv + ofs).r;
			else if(idx == 1) d = tex2D(csmTex1, uv + ofs).r;
			else              d = tex2D(csmTex2, uv + ofs).r;
			vis += step(refZ, d);
		}
		return vis * (1.0/32.0);
	}else if(csmTuning2.x > 0.5){
		// Soft — 16 taps. Picks up 4× the samples vs Sharp for visibly
		// less banding. Worth it on the player's car interior + close
		// foliage where shadow edges are visible.
		[unroll]
		for(int i = 0; i < 16; i++){
			float2 ofs = csmPoisson16[i] * texelStep;
			float d;
			if(idx == 0)      d = tex2D(csmTex0, uv + ofs).r;
			else if(idx == 1) d = tex2D(csmTex1, uv + ofs).r;
			else              d = tex2D(csmTex2, uv + ofs).r;
			vis += step(refZ, d);
		}
		return vis * (1.0/16.0);
	}else{
		// Sharp — 4 taps. Performance fallback for low-end GPUs.
		[unroll]
		for(int i = 0; i < 4; i++){
			float2 ofs = csmPoisson4[i] * texelStep;
			float d;
			if(idx == 0)      d = tex2D(csmTex0, uv + ofs).r;
			else if(idx == 1) d = tex2D(csmTex1, uv + ofs).r;
			else              d = tex2D(csmTex2, uv + ofs).r;
			vis += step(refZ, d);
		}
		return vis * 0.25;
	}
}

float CSMShadowFactor(float3 worldPos, float viewDist)
{
	// Pick cascade by view-space distance from the camera. Soft-fade
	// between cascades within csmTuning.w metres of the boundary so the
	// seam isn't visible.
	float s0 = csmParams.x, s1 = csmParams.y, s2 = csmParams.z;
	int idx = 2;
	if(viewDist < s0) idx = 0;
	else if(viewDist < s1) idx = 1;
	else if(viewDist < s2) idx = 2;
	else return 1.0;	// beyond last cascade

	float vis = CSMSampleCascade(idx, worldPos);

	// Boundary blend — re-sample the next cascade and lerp.
	float blend = csmTuning.w;
	if(blend > 0.0){
		float boundary = (idx == 0) ? s0 : (idx == 1) ? s1 : s2;
		float over = saturate((viewDist - (boundary - blend)) / blend);
		if(over > 0.0 && idx < 2){
			float visNext = CSMSampleCascade(idx + 1, worldPos);
			vis = lerp(vis, visNext, over);
		}
	}
	return vis;
}
#endif

#ifdef GBUFFER
struct PS_out {
	float4 Color		: COLOR0;	// HDR linear scene
	float4 NormalDepth	: COLOR1;	// RGB = world normal * 0.5 + 0.5, A = linear depth
};
#endif


// ComputeShadedColor: shared core that computes the final fragment colour.
// The two main() entry points (single-RT vs MRT) call this and then either
// return float4 (legacy) or pack the result into PS_out with the G-buffer
// channel filled in.
float4 ComputeShadedColor(VS_out input)
{
	float4 color = input.Color;

#ifdef PER_PIXEL_LIGHTING
	float3 N = normalize(input.WorldNormal);
	float3 lit = float3(0.0, 0.0, 0.0);
	float3 spec = float3(0.0, 0.0, 0.0);

	int i;
	// CSM shadow factor — applied multiplicatively to the directional
	// contribution only (point/spot lights ignore CSM since they have no
	// shadow map). csmParams.w is 0 when the host hasn't uploaded
	// cascades — the lerp below cancels the contribution back to 1.0 in
	// that case. (No [branch] here — ps_3_0 forbids dynamic branching
	// with gradient tex reads inside the same scope.)
	float viewDist = distance(input.WorldPos, eyePosPS.xyz);
	float csmRaw   = CSMShadowFactor(input.WorldPos, viewDist);
	float csmShadow = lerp(1.0, csmRaw, saturate(csmParams.w));

	// Wet-surface modulation. Only the up-facing component of the normal
	// gets wet (water pools where gravity points it). Linearly mixes the
	// dry surfDiffuse/surfSpecular with the wet values.
	float wetMask = saturate(N.z) * saturate(wetnessParams.x);

	// Puddle modulation — boost the wet effect locally on flat-ish
	// up-facing surfaces using a cheap procedural splotch pattern. The
	// mask peaks in isolated spots so the eye reads them as actual
	// puddles, not a uniform wetness gradient. Strength=0 fully bypasses.
	[branch]
	if(puddlesParams.x > 0.01){
		float puddleFlat = saturate((N.z - 0.9) * 10.0);	// 0..1 across z=0.9..1.0
		float puddleMask = PuddleMask(input.WorldPos.xy, puddlesParams.y);
		// Boost wetness inside puddle spots; outside (puddleMask ≈ 0)
		// the base wetMask is unchanged.
		float puddleBoost = puddleMask * puddleFlat * puddlesParams.x;
		wetMask = saturate(wetMask + puddleBoost * 0.6);
	}

	float wetDiffuse = lerp(surfDiffuse, surfDiffuse * wetnessParams.y, wetMask);
	float wetSpec    = lerp(surfSpecular, surfSpecular * wetnessParams.z, wetMask);
	float wetPower   = lerp(1.0, max(wetnessParams.w, 1.0), wetMask);

	// Rain ripples — animated normal perturbation. Only fires on flat-ish
	// up-facing wet surfaces (N.z > 0.85), so vehicle bodies / vertical
	// walls don't get a fake wave shimmer. The mask smoothly fades 0.85
	// → 1.0 so the boundary isn't visible. Strength is gated by the
	// per-pixel wetness AND the global rain strength, so dry weather
	// pays the [branch] cost only and nothing more.
	[branch]
	if(rainRipplesParams.y > 0.01){
		float flatMask = saturate((N.z - 0.85) * 6.66);	// 0..1 across z=0.85..1.0
		float ripMask = wetMask * flatMask;
		if(ripMask > 0.01){
			float2 ripN = ComputeRainRippleN(input.WorldPos.xy,
			                                  rainRipplesParams.x,
			                                  rainRipplesParams.y * ripMask,
			                                  rainRipplesParams.z);
			// Up-facing surface: world XY perturbation maps directly to
			// the N.xy. Renormalise so |N| = 1 for the dot products below.
			N = normalize(float3(N.x + ripN.x, N.y + ripN.y, N.z));
		}
	}
#ifdef DIRECTIONALS
	[loop]
	for(i = 0; i < numDirLights; i++)
		lit += DoDirLight(lights[i+firstDirLight], N) * wetDiffuse * csmShadow;
#endif
#ifdef POINTLIGHTS
	[loop]
	for(i = 0; i < numPointLights; i++)
		lit += DoPointLight(lights[i+firstPointLight], input.WorldPos, N) * wetDiffuse;
#endif
#ifdef SPOTLIGHTS
	[loop]
	for(i = 0; i < numSpotLights; i++)
		lit += DoSpotLight(lights[i+firstSpotLight], input.WorldPos, N) * wetDiffuse;
#endif

	// Dynamic point lights — host-side selected top-N nearby CPointLights
	// applied per-pixel. This is the path that lets car headlights / lamps
	// / muzzle flashes / explosions illuminate buildings + props that the
	// legacy CEntity::SetupLighting() path bypassed.
	//
	// NOT modulated by surfDiffuse/wetDiffuse — many world materials have
	// surfaceProps.diffuse = 0 (the engine relies on baked vertex colours
	// for the diffuse term on static geometry), which would zero out our
	// contribution. The light still gets the material's albedo modulation
	// later via `baseLight * matCol.rgb`, so dark paint still reflects
	// less; this just guarantees the dynamic source actually reaches the
	// pixel. A small (1 - 0.5*wetMask) factor keeps wet-asphalt darkening
	// intact without killing the contribution on vertical surfaces.
	float dynScale = 1.0 - 0.5 * wetMask;
	lit += ApplyDynamicPointLights(input.WorldPos, N) * dynScale;

	[branch]
	if(surfSpecular > 0.001 || wetMask > 0.05){
		float3 V = normalize(eyePosPS.xyz - input.WorldPos);
		float power = max(eyePosPS.w, 8.0) * wetPower;
#ifdef DIRECTIONALS
		[loop]
		for(i = 0; i < numDirLights; i++)
			spec += DoDirLightSpec(lights[i+firstDirLight], N, V, power) * wetSpec;
#endif
		// Specular respects the same CSM occluder as diffuse — a shadowed
		// surface shouldn't sparkle in the sun.
		spec *= csmShadow;
	}

	// IBL — procedural hemisphere gradient driven by world normal. iblParams.x
	// gates the whole contribution; when the host sets it to 0 we trade three
	// mul-adds for nothing. The horizon weight uses 1 - |N.z|^p so a larger
	// exponent gives a sharper sky/horizon transition (good for daytime).
	// Phase 2: when iblParams.z > 0.5 we sample the real irradiance cube
	// instead (one texCUBE) — captures sun bleed + directional sky pattern
	// the analytic gradient can't represent.
	{
		float up   = saturate( N.z);
		float down = saturate(-N.z);
		float hor  = 1.0 - saturate(pow(abs(N.z), max(iblParams.y, 0.5)));
		float3 gradientCol = up   * iblSky.rgb
		                   + down * iblGround.rgb
		                   + hor  * iblHorizon.rgb;
		float3 cubeCol = texCUBE(iblIrradianceCube, N).rgb;
		float useCube = step(0.5, iblParams.z);
		float3 iblCol = lerp(gradientCol, cubeCol, useCube);
		// Treat IBL as a soft diffuse — modulate by the material diffuse
		// coefficient so unlit materials (like UI quads, particles) don't
		// pick up sky bleed when this branch is somehow hit.
		lit += iblCol * iblParams.x * surfDiffuse;
	}

	// In LDR mode we clamp prelight+ambient+lit BEFORE matCol — matches the
	// legacy VS path so brightness stays identical to stock GTA.
	// In HDR/G-buffer mode we only reject negatives so >1 light values
	// survive into the bloom + tonemap pipeline.
#ifdef GBUFFER
	float3 baseLight = max(color.rgb + lit, 0.0);
#else
	float3 baseLight = saturate(color.rgb + lit);
#endif
	color.rgb = baseLight * matCol.rgb + spec;
	color.a *= matCol.a;

	// Image-based specular reflection — sample the live capture cube
	// along the world-space reflection vector, Fresnel-weight it,
	// modulate by the material specular term, add on top. This gives
	// every reflective surface (buildings, road, peds — not just
	// cars) a directional sky reflection. The cube binding shares
	// the iblIrradianceCube handle when the host doesn't bind a
	// distinct reflection cube, so even in fallback mode there's a
	// usable reflection source.
	{
		float3 V = normalize(eyePosPS.xyz - input.WorldPos);
		float NoV = saturate(dot(N, V));
		float oneMinus = 1.0 - NoV;
		float f5 = oneMinus * oneMinus; f5 *= f5 * oneMinus;
		float F0 = 0.04;	// dielectric default
		float fresnel = F0 + (1.0 - F0) * f5;

		float3 R = reflect(-V, N);
		float3 reflectionColor = texCUBE(iblReflectionCube, R).rgb;
		// Surface-specular and the user-set strength gate the contribution.
		color.rgb += reflectionColor * fresnel * surfSpecular * iblReflParams.x;
	}
#endif

#ifdef TEX
	color *= tex2D(tex0, input.TexCoord0.xy);
#endif

	// Fog: linear lerp. A smoothstep variant was tried once and pushed mid-
	// distance pixels toward fogColor — looked blue in Vice City.
	color.rgb = lerp(fogColor.rgb, color.rgb, input.TexCoord0.z);
	return color;
}


#ifdef GBUFFER
PS_out main(VS_out input)
{
	PS_out o;
	o.Color = ComputeShadedColor(input);

	// World-space normal packed into [0,1] for RGBA16F storage. Re-normalise
	// before storage so the consumer (SSAO, CSM) gets unit-length normals
	// even on degenerate mesh edges.
	float3 N = normalize(input.WorldNormal);
	o.NormalDepth = float4(N * 0.5 + 0.5, saturate(input.ViewDepth));
	return o;
}
#else
float4 main(VS_out input) : COLOR
{
	return ComputeShadedColor(input);
}
#endif
