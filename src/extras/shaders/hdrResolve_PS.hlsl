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

// Volumetric spotlights — up to 4 brightest active scene point/spot
// lights, accumulated into the existing volumetric ray-march as
// additional in-scatter sources. Each light:
//   pos.xyz = world position
//   pos.w   = radius² (distance falloff cutoff)
//   col.rgb = HDR colour
//   col.a   = intensity multiplier (0 = disabled slot)
float4 volSpotPos[4] : register(c25);
float4 volSpotCol[4] : register(c29);

float3 ACES(float3 x)
{
	return saturate((x*(2.51*x + 0.03)) / (x*(2.43*x + 0.59) + 0.14));
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

	// Volumetric fog — ray-march along the world-space view ray, integrate
	// scattering with a HG phase function biased toward the sun. Branch is
	// static so non-fog frames pay only the if() test.
	[branch]
	if(volParams.w > 0.001){
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
		// Use the normalized forward direction of the ray.
		float3 fwd = normalize(rayDir);
		float cosTheta = dot(fwd, volSun.xyz);
		float phase = HGPhase(cosTheta, volSun.w);

		const int STEPS = 12;
		float stepLen = maxD / (float)STEPS;

		// Per-pixel jitter — kills banding from low step count.
		float jitter = frac(sin(dot(uv * 4321.123, float2(12.9898, 78.233))) * 43758.5453);

		float3 scatter = float3(0, 0, 0);
		float trans = 1.0;

		[loop]
		for(int i = 0; i < STEPS; i++){
			float t = (float(i) + jitter) * stepLen;
			float3 wp = volCamera.xyz + fwd * t;

			// Exponential height fog — heavier near the ground.
			float h = max(0.0, wp.z - volParams.y);
			float density = volColor.w * exp(-h * volParams.x);

			// Beer-Lambert extinction along the segment.
			float segOpt = density * stepLen;
			// In-scatter contribution from the sun, weighted by phase.
			// Lights scale included in volColor.xyz so we don't need to
			// multiply by sun colour again.
			float3 inScat = volColor.xyz * phase * density * stepLen;

			// Volumetric spotlights — for each active scene light, add
			// an isotropic in-scatter contribution proportional to
			// density × inverse-square distance × radius-cutoff. Cheap
			// (~16 ALU per active light per step) and the light's own
			// intensity gates contribution to nothing when its slot is
			// disabled.
			[unroll]
			for(int li = 0; li < 4; li++){
				float3 toLight = volSpotPos[li].xyz - wp;
				float d2 = dot(toLight, toLight) + 1e-3;
				float radius2 = volSpotPos[li].w;
				float falloff = saturate(1.0 - d2 / max(radius2, 1.0));
				falloff *= falloff;	// quadratic falloff for plausibility
				float3 lightContrib = volSpotCol[li].rgb * volSpotCol[li].a
				                    * falloff * density * stepLen
				                    * (1.0 / max(d2 * 0.05, 1.0));
				inScat += lightContrib;
			}

			scatter += inScat * trans;
			trans *= exp(-segOpt);
		}

		// Lerp by enable strength so the menu slider can attenuate the
		// effect without zeroing density (keeps the lookup colour intact).
		float3 fogged = col * trans + scatter;
		col = lerp(col, fogged, saturate(volParams.w));
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
		float3 N = normalize(gbuf.rgb * 2.0 - 1.0);

		// View direction at this fragment — use the volumetric ray since
		// it carries the same world-space ray we want here.
		float3 fwd = normalize(lerp(lerp(volRayTL.xyz, volRayTR.xyz, uv.x),
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


	// ACES filmic tonemap, blendable.
	float3 aces = ACES(col);
	col = lerp(col, aces, hdrTonemap.y);

	// Saturation in luminance space.
	float lum = dot(col, float3(0.2126, 0.7152, 0.0722));
	col = lerp(float3(lum, lum, lum), col, hdrTonemap.w);

	// Gamma 2.2 encode for the LDR backbuffer.
	float3 gam = pow(max(col, 1e-5), 1.0/2.2);
	col = lerp(col, gam, hdrTonemap.z);

	return float4(saturate(col), 1.0);
}
