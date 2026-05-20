// Screen-Space Global Illumination (SSGI) — 1-bounce indirect light.
//
// For each pixel: sample 4 random hemispherical directions biased by
// the surface normal (and optionally the Stage 31 bent normal), march
// each ray a few steps through screen space, and at each hit accumulate
// the bounce radiance from pHdrScene. Output: a half-res bounce-light
// buffer that hdrResolve_PS adds additively to the final scene.
//
// Sample budget for ps_3_0:
//   4 directions × 6 march steps = 24 samples per pixel.
//   Each sample = 2 tex2D fetches (gbuf for depth + hdr for radiance).
//   Total per pixel: ~48 fetches, ~300 ALU instructions.
//   Comfortably within ps_3_0's 65535 static-instruction limit.
//
// Compared to a "full quality" 16×16 SSGI this is sparse — temporal
// accumulation via TAA (Stage 14 already provides) smooths the noise.
// Menu should recommend "TAA on" when SSGI is enabled; without TAA
// the result reads as grainy on flat surfaces.
//
// Bindings:
//   s0 = pGbufNormalDepth — RGB = world normal*0.5+0.5, A = linear viewZ/farClip
//   s1 = pHdrScene        — linear HDR scene radiance (source for bounce)
//   s2 = pSsaoA           — Stage 31 bent normal in .gba (optional direction bias)
//
// Constants:
//   c10: SSGI params (.x = strength 0..1, .y = max distance world units,
//        .z = step count clamp, .w = NdotL gate)
//   c11: camera world position + farClip
//   c12..c15: frustum corner rays TL/TR/BR/BL
//   c16..c19: world-to-clip view × proj 4×4

sampler2D gbufTex : register(s0);
sampler2D hdrTex  : register(s1);
sampler2D bentTex : register(s2);

float4 ssgiParams : register(c10);
float4 ssgiCamera : register(c11);
float4 ssgiRayTL  : register(c12);
float4 ssgiRayTR  : register(c13);
float4 ssgiRayBR  : register(c14);
float4 ssgiRayBL  : register(c15);
float4x4 ssgiViewProj : register(c16);

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

// NaN-safe normalize — same defensive pattern as SSR / TAA.
float3 SafeNormalize(float3 v)
{
	float l2 = dot(v, v);
	return v * rsqrt(max(l2, 1e-8));
}

// Project world-space point to UV + clip-Z. Mirror of the SSR helper.
float3 WorldToUVDepth(float3 wp)
{
	float4 clip = mul(float4(wp, 1.0), ssgiViewProj);
	clip.xyz /= max(clip.w, 1e-5);
	float2 uv = clip.xy * 0.5 + 0.5;
	uv.y = 1.0 - uv.y;
	return float3(uv, clip.z);
}

// 4 quasi-random hemisphere directions in tangent space (z = up). Same
// 4-direction set every pixel — TAA's per-frame jitter shuffles the
// effective sample set across frames, so the spatial set doesn't need
// to be temporal.
static const float3 kHemiDir[4] = {
	float3( 0.514,  0.514, 0.687),	// upper-right slope
	float3(-0.514,  0.514, 0.687),	// upper-left slope
	float3(-0.514, -0.514, 0.687),	// lower-left slope
	float3( 0.514, -0.514, 0.687),	// lower-right slope
};

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float4 gbuf = tex2D(gbufTex, uv);

	// Sky / cleared / no-surface pixels → no bounce.
	if(gbuf.a < 0.0005 || ssgiParams.x < 0.001)
		return float4(0, 0, 0, 0);

	float3 N = SafeNormalize(gbuf.rgb * 2.0 - 1.0);

	// View ray + world position (same reconstruction as SSR / TAA).
	float3 ray = lerp(lerp(ssgiRayTL.xyz, ssgiRayTR.xyz, uv.x),
	                  lerp(ssgiRayBL.xyz, ssgiRayBR.xyz, uv.x),
	                  uv.y);
	float viewZ = gbuf.a * ssgiCamera.w;
	float3 wp = ssgiCamera.xyz + ray * viewZ;
	float3 V = SafeNormalize(ray);

	// Read Stage 31 bent normal from ssao .gba and lerp N toward it so
	// our sampling hemisphere leans into the more-open region. If the
	// bent normal isn't meaningful (e.g. SSAO disabled), .gba reads as
	// the surface N fallback that ssao_PS / hbao_PS write, so the
	// blend is a no-op (N + (N-something else) = roughly N).
	float3 bentN_screen = tex2D(bentTex, uv).gba * 2.0 - 1.0;
	// bentN_screen is in screen-tangent space; for a rough world-space
	// approximation we treat its XY as a small tangent perturbation and
	// keep N's world Z. Adequate for a direction bias.
	float3 effectiveN = SafeNormalize(N + float3(bentN_screen.xy * 0.3, 0));

	// Build TBN around effectiveN for hemisphere sampling.
	float3 up = abs(effectiveN.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
	float3 T  = SafeNormalize(cross(up, effectiveN));
	float3 B  = cross(effectiveN, T);

	float maxDist  = ssgiParams.y;
	int   stepN    = (int)clamp(ssgiParams.z, 3.0, 8.0);
	float stepLen  = maxDist / (float)stepN;
	float ndotlGate = ssgiParams.w;

	// Per-pixel jitter so adjacent pixels sample at different start
	// distances along their rays — TAA averages out the noise next
	// frame.
	float jitter = frac(sin(dot(uv * 7.91, float2(12.9898, 78.233))) * 43758.5453);

	float3 acc = float3(0, 0, 0);

	[unroll]
	for(int d = 0; d < 4; d++){
		// Rotate the direction set by the per-pixel jitter angle so
		// neighbouring pixels don't all share the same 4 samples.
		float a = jitter * 6.28318;
		float ca = cos(a), sa = sin(a);
		float3 dlocal = kHemiDir[d];
		float3 dirT = float3(dlocal.x*ca - dlocal.y*sa, dlocal.x*sa + dlocal.y*ca, dlocal.z);
		// Transform into world space via TBN.
		float3 R = T * dirT.x + B * dirT.y + effectiveN * dirT.z;

		// Skip rays going AWAY from the camera (back-facing rays);
		// would always escape screen anyway and waste samples.
		if(dot(R, V) > 0.95)
			continue;

		[loop]
		for(int i = 1; i <= 8; i++){
			float doStep = step(float(i), float(stepN));
			float t = ((float)i + jitter) * stepLen;
			float3 sp = wp + R * t;

			float3 spProj = WorldToUVDepth(sp);
			float2 spUV = spProj.xy;
			float onScreen = step(0.0, spUV.x) * step(0.0, spUV.y)
			               * step(spUV.x, 1.0) * step(spUV.y, 1.0);

			// Sample gbuf depth at hit candidate (lod 0 → no gradient issue).
			float4 spGbuf = tex2Dlod(gbufTex, float4(spUV, 0, 0));
			float spViewZ = spGbuf.a * ssgiCamera.w;
			float spRayZ  = length(sp - ssgiCamera.xyz);
			float dz      = spRayZ - spViewZ;

			float spHasGeo = step(0.0005, spGbuf.a);
			// Hit window — sample is in front of the actual surface by
			// at most stepLen × 1.5 (thickness tolerance).
			float thickness = stepLen * 1.5;
			float hit = doStep * onScreen * spHasGeo
			          * step(0.0, dz) * step(dz, thickness);

			if(hit > 0.5){
				// Sample HDR at the hit point and accumulate.
				float3 spN = SafeNormalize(spGbuf.rgb * 2.0 - 1.0);
				// Lambertian-style cosine weight at receiver.
				float ndotl = saturate(dot(effectiveN, R));
				if(ndotl > ndotlGate){
					float4 rad = tex2Dlod(hdrTex, float4(spUV, 0, 0));
					// Distance falloff so far hits contribute less.
					float falloff = saturate(1.0 - t / maxDist);
					acc += rad.rgb * ndotl * falloff;
				}
				break;
			}
		}
	}

	// Average over directions (4 = 1/4 = 0.25) + apply strength.
	acc *= 0.25 * ssgiParams.x;

	return float4(acc, 1.0);
}
