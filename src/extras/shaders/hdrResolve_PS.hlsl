// HDR -> LDR resolve shader with optional SSAO compose + volumetric fog.
//
// Reads the RGBA16F off-screen scene RT (CGBuffer::pHdrScene), optionally
// multiplies by an SSAO mask (CPostFX::pSsaoA), composes a single-scattering
// height fog with directional in-scatter from the sun, then applies the
// final tonemap pipeline (exposure -> ACES -> saturation -> gamma) before
// writing to the LDR backbuffer.

sampler2D hdrTex  : register(s0);
sampler2D ssaoTex : register(s1);	// R8 AO; 1.0 = fully lit
sampler2D gbufTex : register(s2);	// RGB = world-normal*0.5+0.5, A = linearDepth (viewZ/farClip)
sampler2D ssrTex  : register(s3);	// RGBA = reflection colour + confidence
sampler2D ssgiTex : register(s4);	// RGBA16F bounce radiance (rgb = indirect light)

// .x = exposure (linear multiplier, 1.0 = neutral)
// .y = ACES toggle (0..1, lerps toward filmic curve)
// .z = gamma toggle (0..1)
// .w = saturation (1.0 = neutral)
float4 hdrTonemap : register(c10);

// .x = SSAO strength (0 = disabled), .y = AO power curve, .zw = unused
float4 hdrSsao : register(c11);

// .xyz = camera world position, .w = farClip (for unpacking gbuf.a → viewZ)
float4 volCamera : register(c12);

// World-space unit rays through each screen corner (length = 1 forward).
// Bilerped by uv to get the per-pixel ray. Ordering: uv=(0,0)=TL, (1,1)=BR.
float4 volRayTL : register(c13);
float4 volRayTR : register(c14);
float4 volRayBR : register(c15);
float4 volRayBL : register(c16);

// .xyz = direction toward the sun (normalized), .w = Henyey-Greenstein g
//        (0 = isotropic, 0.7 = forward-scattered halo around the sun)
float4 volSun : register(c17);

// .xyz = scattering colour (sun + ambient sky, can be >1 HDR), .w = base density
//        (extinction per metre at sea level; ~0.01..0.05 typical)
float4 volColor : register(c18);

// .x = height falloff (1/m, larger = fog thins faster with altitude)
// .y = ground world-Z (heights below this fully attenuated to base density)
// .z = max march distance in metres (used when the fragment is sky / depth=0)
// .w = enable flag (0 = bypass, >0 = on; also controls overall strength lerp)
float4 volParams : register(c19);

// VolFog raymarch quality — .x = step count (clamped 4..32 by the host).
// Pulled out of the shader so the menu's CCFOSelect can dial cost vs
// quality without a re-link. The legacy 12-step value is the .x=12 path.
// Sits at c41 because c25..c40 is the volSpot pos+col array block.
float4 volQuality : register(c41);

// SSR compose. .x = strength (0 = off), .y = Schlick F0 bias,
// .z = sky-fallback strength (0 = none, 1 = full IBL sky as miss
// fallback so reflections never go pitch-black)
// .w = reserved
float4 ssrCompose : register(c20);

// Camera world position again (matches volCamera.xyz) — kept separate so
// SSR-only frames don't depend on the volumetric block. .w = unused.
float4 ssrViewer : register(c21);

// IBL sky/horizon/ground colours (mirror of librw c44..c46) so SSR misses
// can fall back to the procedural sky gradient instead of black.
float4 ssrIblSky     : register(c22);
float4 ssrIblHorizon : register(c23);
float4 ssrIblGround  : register(c24);

// Volumetric spotlights — up to 8 brightest active scene point/spot
// lights, accumulated into the existing volumetric ray-march as
// additional in-scatter sources. Doubled from 4 to 8 so dense night
// scenes (street lamps + headlights + muzzle flash) don't lose cones.
// Each light:
//   pos.xyz = world position
//   pos.w   = radius² (distance falloff cutoff)
//   col.rgb = HDR colour
//   col.a   = master enable (0 = disabled slot; the host now drives
//             this independently of the global VolFog toggle so cones
//             still show up at night when full-screen fog is off).
float4 volSpotPos[8] : register(c25);
float4 volSpotCol[8] : register(c33);

// SSGI compose — .x = strength multiplier on the per-pixel bounce buffer
// (0 = bypass via [branch]; the SSGI pass also clears to black so a stale
// pSsgiA can't bleed in if the host forgets to disable). Lives at c42 to
// stay outside the SSR/vol blocks; safe to extend with quality dials later
// (e.g. .y = AO modulation, .z = sky-bounce gate) without touching binders.
float4 ssgiCompose : register(c42);

// Stage 32 — bent-normal-aware ambient bias. GTAO writes the average
// unoccluded direction into pSsaoA.gba (.r is AO). When bentBias.x > 0
// the resolve pass adds an ambient *correction* equal to the difference
// between the IBL gradient evaluated at the bent normal vs the surface
// normal, weighted by AO. Net effect: ambient leans away from occluded
// directions (the back wall of an alcove fills less, the open side
// fills more) while keeping the energy budget bounded by AO×strength.
//   .x = strength (0 = bypass entire block)
//   .y = bias toward bent N (0 = surface N only, 1 = full bent N)
//   .z = reserved (future: per-surface gating)
//   .w = reserved
float4 bentParams : register(c43);

float3 ACES(float3 x)
{
	return saturate((x*(2.51*x + 0.03)) / (x*(2.43*x + 0.59) + 0.14));
}

// NaN-safe normalize. The standard normalize() returns NaN when |v| ≈ 0,
// and a single NaN propagating through HG phase / SSR reflect() makes the
// whole pixel — and via temporal accumulation, a whole region — black.
// rsqrt(max(l2, 1e-8)) returns ~10000 for a zero vector, so the result is
// finite (just an arbitrary direction). That's still degenerate input but
// it stays inside the float range and downstream saturate()/lerp() recover.
float3 SafeNormalize(float3 v)
{
	float l2 = dot(v, v);
	return v * rsqrt(max(l2, 1e-8));
}

// Henyey-Greenstein phase function — gives the directional brightening
// when the camera looks toward the sun.
float HGPhase(float cosTheta, float g)
{
	float g2 = g * g;
	float denom = 1.0 + g2 - 2.0 * g * cosTheta;
	// pow(x, 1.5) — branchless approximation good enough for fog
	return (1.0 - g2) / (4.0 * 3.14159265 * denom * sqrt(denom));
}

float4 main(in float2 uv : TEXCOORD0) : COLOR0
{
	float3 col = tex2D(hdrTex, uv).rgb;

	// SSAO compose — multiply ambient term by AO. Skipped when strength = 0
	// so the shader works correctly even when SSAO RTs are stale/disabled.
	[branch]
	if(hdrSsao.x > 0.001){
		float ao = tex2D(ssaoTex, uv).r;
		ao = pow(saturate(ao), max(hdrSsao.y, 0.1));
		ao = lerp(1.0, ao, hdrSsao.x);
		col *= ao;
	}

	// Stage 32 — bent-normal ambient bias. Reads the GTAO bent normal from
	// the same ssaoTex (.gba) and adds an additive ambient correction
	// equal to the IBL gradient at the bent normal MINUS the gradient at
	// the surface normal. With AO weighting this becomes "fill the open
	// hemisphere a bit more, fill the closed one a bit less" — the visual
	// signature of bent normals without re-running the receiver IBL.
	// SSAO must also be active (the .gba channel is only meaningful when
	// the GTAO/SSAO pass wrote it; the SSAO/HBAO fallback writes surface
	// N, so the delta is zero and the term is a no-op without GTAO).
	[branch]
	if(bentParams.x > 0.001 && hdrSsao.x > 0.001){
		float4 aoSample = tex2D(ssaoTex, uv);
		float4 gbufN    = tex2D(gbufTex, uv);
		float3 surfN    = SafeNormalize(gbufN.rgb * 2.0 - 1.0);
		float3 bentN    = SafeNormalize(aoSample.gba * 2.0 - 1.0);
		// Treat bent N as a small perturbation of surface N — the SSAO
		// pass stores it in screen-tangent space, but its overall up
		// component is a reasonable proxy for world-space "open-sky".
		// Lerp gives the artist a per-scene dial: 0 = ignore bent N,
		// 1 = trust it fully.
		float3 effN = SafeNormalize(lerp(surfN, bentN, bentParams.y));

		// Procedural sky gradient at both normals — same up/horizon/down
		// split the SSR fallback uses. We don't ship a full IBL cube
		// sample here because the receiver already ran that path; the
		// goal is a cheap delta, not a re-bake.
		float upS = saturate( surfN.z); float dnS = saturate(-surfN.z);
		float hoS = 1.0 - saturate(abs(surfN.z));
		float3 ambSurf = upS * ssrIblSky.rgb + dnS * ssrIblGround.rgb + hoS * ssrIblHorizon.rgb;

		float upE = saturate( effN.z);  float dnE = saturate(-effN.z);
		float hoE = 1.0 - saturate(abs(effN.z));
		float3 ambEff  = upE * ssrIblSky.rgb + dnE * ssrIblGround.rgb + hoE * ssrIblHorizon.rgb;

		// Re-fetch AO so the delta is weighted by how occluded we are —
		// fully unoccluded surfaces (aoSample.r=1) get the largest bias,
		// open sky pixels (where AO is ≈ 1 by design) get a clean signal,
		// and pixels with no meaningful bent N (aoSample.r ≈ 1, bentN ≈
		// surfN) naturally produce delta ≈ 0 → no visible change.
		float ao = aoSample.r;
		col += (ambEff - ambSurf) * (bentParams.x * ao);
	}

	// Volumetric fog + spotlight cones — single ray-march that integrates
	// height-fog in-scatter from the sun AND per-light in-scatter from up
	// to 8 nearby point lights. The two systems used to share one gate
	// (volParams.w > 0); they're now independent so the player can leave
	// the global height fog OFF while still seeing headlight / muzzle-
	// flash cones at night. The march runs if EITHER subsystem is active.
	float spotEnableSum = volSpotCol[0].a + volSpotCol[1].a + volSpotCol[2].a + volSpotCol[3].a
	                    + volSpotCol[4].a + volSpotCol[5].a + volSpotCol[6].a + volSpotCol[7].a;
	[branch]
	if(volParams.w > 0.001 || spotEnableSum > 0.001){
		// Bilerp the world-space ray direction from the 4 corner vectors.
		// uv.y = 0 = top in D3D9 texture coords, which matches the TL ray.
		float3 rayDir = lerp(lerp(volRayTL.xyz, volRayTR.xyz, uv.x),
		                     lerp(volRayBL.xyz, volRayBR.xyz, uv.x),
		                     uv.y);
		// rayDir has a unit forward component, so |rayDir| > 1 at corners.
		// Marching by view-space Z still gives correct world positions
		// because rayDir.z (forward proj) = 1 by construction.

		float4 gbuf = tex2D(gbufTex, uv);
		// gbuf.a is in [0,1] linear viewZ / farClip. Sky / un-rendered
		// pixels read 0 (gbuffer was cleared with A=0) — treat as the
		// max-march distance so we still get inscatter into the sky.
		float viewZ = gbuf.a * volCamera.w;
		float maxD = (gbuf.a > 0.0005) ? viewZ : volParams.z;
		maxD = min(maxD, volParams.z);

		// Phase function — directional brightening toward the sun.
		// Use the normalized forward direction of the ray. NaN-safe so
		// degenerate frustum corners (when |rayDir|² → 0 at certain
		// camera FOV/aspect combinations) don't propagate black through
		// the whole ray-march.
		float3 fwd = SafeNormalize(rayDir);
		float cosTheta = dot(fwd, volSun.xyz);
		float phase = HGPhase(cosTheta, volSun.w);

		// Host-driven step count. Clamp inside the shader as belt-and-
		// braces against junk uploads — the [loop] cost scales linearly
		// so a runaway value is more annoying than dangerous.
		int STEPS = (int)clamp(volQuality.x, 4.0, 32.0);
		float stepLen = maxD / (float)STEPS;

		// Per-pixel jitter — kills banding from low step count *when fog
		// is active*. Without VolFog the raymarch only integrates spot
		// cones, and the jitter pattern then shows through as a static
		// speckle across the whole screen because there's no fog density
		// to absorb it and TAA (which would smooth it) is off by default.
		// Use a fixed half-step start when fog is off — slight per-pixel
		// banding is invisible vs the static-noise alternative.
		float jitter = (volParams.w > 0.001)
		    ? frac(sin(dot(uv * 4321.123, float2(12.9898, 78.233))) * 43758.5453)
		    : 0.5;

		float3 scatter = float3(0, 0, 0);
		float trans = 1.0;

		// Spot cones need at least *some* particulate density to scatter
		// visibly off; without this baseline, cones disappear in thin air
		// when the global VolFogDensity is small (or zero with fog off).
		// 0.005 ≈ a faint clear-night haze — invisible without a light
		// source, properly visible inside a cone.
		float spotMediumBase = 0.005;

		[loop]
		for(int i = 0; i < STEPS; i++){
			float t = (float(i) + jitter) * stepLen;
			float3 wp = volCamera.xyz + fwd * t;

			// Exponential height fog — heavier near the ground. Drops to
			// 0 when VolFog is off (volColor.w → 0 from host).
			float h = max(0.0, wp.z - volParams.y);
			float density = volColor.w * exp(-h * volParams.x);

			// Beer-Lambert extinction along the segment. Extinction
			// follows real fog density only — spot baseline is *not*
			// added here, otherwise toggling spots would dim the picture.
			float segOpt = density * stepLen;
			// In-scatter contribution from the sun, weighted by phase.
			// Lights scale included in volColor.xyz so we don't need to
			// multiply by sun colour again.
			float3 inScat = volColor.xyz * phase * density * stepLen;

			// Effective scattering medium for spot cones — the larger of
			// the real fog density and a thin-air baseline, so cones are
			// visible even when global fog is off.
			float spotMedium = max(density, spotMediumBase);

			// Volumetric spotlights — for each active scene light, add
			// an isotropic in-scatter contribution proportional to
			// medium × inverse-square distance × radius-cutoff. Cheap
			// (~16 ALU per active light per step) and the light's own
			// col.a gates contribution to nothing when its slot is empty.
			[unroll]
			for(int li = 0; li < 8; li++){
				float3 toLight = volSpotPos[li].xyz - wp;
				float d2 = dot(toLight, toLight) + 1e-3;
				float radius2 = volSpotPos[li].w;
				float falloff = saturate(1.0 - d2 / max(radius2, 1.0));
				falloff *= falloff;	// quadratic falloff for plausibility
				float3 lightContrib = volSpotCol[li].rgb * volSpotCol[li].a
				                    * falloff * spotMedium * stepLen
				                    * (1.0 / max(d2 * 0.05, 1.0));
				inScat += lightContrib;
			}

			scatter += inScat * trans;
			trans *= exp(-segOpt);
		}

		// Combine — effect strength is max(fog strength, spot strength).
		// With fog off (volParams.w=0) but spots on, trans≈1 so we get
		// pure additive scatter; with fog on, full march behaviour as
		// before. Cap spot strength low (* 0.1) so 8 enabled slots don't
		// add up to a full lerp at every pixel — the spot scatter is
		// supposed to be a *subtle cone glow*, not a screen-wide tint,
		// and the lower cap also keeps any residual jitter below the
		// noise floor when TAA isn't there to smooth it.
		float spotStrength = saturate(spotEnableSum * 0.1);
		float effectStrength = saturate(max(volParams.w, spotStrength));
		float3 fogged = col * trans + scatter;
		col = lerp(col, fogged, effectStrength);
	}

	// Screen-Space Reflections — bilinearly up-sampled from the half-res
	// SSR raster, weighted by a view-dependent Fresnel term so glancing
	// angles get full reflection and head-on views fade to nothing.
	// When the SSR ray escaped the screen (ssr.a ≈ 0), the IBL sky
	// gradient is used as fallback so reflections never go pitch-black.
	[branch]
	if(ssrCompose.x > 0.001){
		float4 ssr = tex2D(ssrTex, uv);
		float4 gbuf = tex2D(gbufTex, uv);
		// Sky / cleared pixels have alpha==0 and a zero-length normal —
		// SafeNormalize keeps the math finite so we don't black out the
		// SSR pass on skybox / muzzle-flash particles.
		float3 N = SafeNormalize(gbuf.rgb * 2.0 - 1.0);

		// View direction at this fragment — use the volumetric ray since
		// it carries the same world-space ray we want here.
		float3 fwd = SafeNormalize(lerp(lerp(volRayTL.xyz, volRayTR.xyz, uv.x),
		                                lerp(volRayBL.xyz, volRayBR.xyz, uv.x),
		                                uv.y));
		float NoV = saturate(dot(N, -fwd));
		// Schlick Fresnel: F = F0 + (1 - F0) * (1 - NoV)^5
		float F0 = ssrCompose.y;
		float oneMinus = 1.0 - NoV;
		float f5 = oneMinus * oneMinus; f5 *= f5 * oneMinus;
		float fresnel = F0 + (1.0 - F0) * f5;

		// SSR miss fallback — sample the IBL sky gradient along the
		// reflected world-space direction. The reflection vector is
		// (V mirrored across N); we pick the colour by the same up/
		// horizon/down hemisphere split the IBL receiver uses.
		float3 R = reflect(fwd, N);
		float upW   = saturate( R.z);
		float downW = saturate(-R.z);
		float horW  = 1.0 - saturate(abs(R.z));
		float3 fallbackCol = upW   * ssrIblSky.rgb
		                   + downW * ssrIblGround.rgb
		                   + horW  * ssrIblHorizon.rgb;

		float3 reflectionCol = lerp(fallbackCol * ssrCompose.z, ssr.rgb, ssr.a);
		float w = max(ssr.a, ssrCompose.z) * fresnel * ssrCompose.x;
		col = lerp(col, reflectionCol, saturate(w));
	}

	// Screen-Space Global Illumination — additive bounce light from the
	// half-res SSGI pass. The compose sits AFTER SSR (specular) and BEFORE
	// exposure so it joins the same tonemap chain as direct light, which
	// keeps the bounce energy roll-off looking natural (ACES + saturation
	// applied uniformly). Modulated by the existing SSAO mask so corners
	// that are heavily occluded don't double-up indirect contribution.
	[branch]
	if(ssgiCompose.x > 0.001){
		float3 bounce = tex2D(ssgiTex, uv).rgb;
		// Re-use the SSAO mask the user already paid for — the bounce
		// gather doesn't account for short-range contact occlusion, and
		// stacking AO × SSGI prevents bleed under cars, sofas, etc.
		float aoMask = 1.0;
		if(hdrSsao.x > 0.001){
			float ao = tex2D(ssaoTex, uv).r;
			ao = pow(saturate(ao), max(hdrSsao.y, 0.1));
			aoMask = lerp(1.0, ao, hdrSsao.x);
		}
		col += max(bounce, 0.0) * ssgiCompose.x * aoMask;
	}


	// Exposure — linear multiplier before tonemap. ACES expects scene
	// linear values around [0..4] for the highlights to roll off correctly,
	// so the host pushes ~1.2 as the default HDR scene midpoint.
	col *= hdrTonemap.x;

	// Clamp HDR colour to non-negative before tonemap. pHdrScene is
	// RGBA16F which legitimately holds negatives (alpha-blend accumulation
	// can sign-flip a channel during overlap), and ACES amplifies the
	// negative range into visible NaN-like sparkle when subsequent
	// saturate() snaps them through zero. The clamp is one ALU op and
	// kills the "HDR noise" symptom on scenes with heavy particle alpha.
	col = max(col, 0.0);

	// ACES filmic tonemap, blendable.
	float3 aces = ACES(col);
	col = lerp(col, aces, hdrTonemap.y);

	// Saturation in luminance space.
	float lum = dot(col, float3(0.2126, 0.7152, 0.0722));
	col = lerp(float3(lum, lum, lum), col, hdrTonemap.w);

	// Gamma 2.2 encode for the LDR backbuffer. ACES already includes an
	// sRGB-ish rolloff, so when ACES toggle is on we must NOT also apply
	// gamma — otherwise the picture gets crushed shadows + washed mids
	// ("HDR but dark as night" symptom). The mutex below disables gamma
	// proportionally to ACES strength.
	float gammaApply = saturate(hdrTonemap.z) * (1.0 - saturate(hdrTonemap.y));
	float3 gam = pow(max(col, 1e-5), 1.0/2.2);
	col = lerp(col, gam, gammaApply);

	return float4(saturate(col), 1.0);
}
