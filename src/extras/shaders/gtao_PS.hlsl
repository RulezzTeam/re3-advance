// Ground-Truth Ambient Occlusion (GTAO) + Bent Normal extension.
//
// Compared with the classic Crytek hemisphere sampler in ssao_PS, GTAO
// computes occlusion from horizon angles rather than samples around a
// hemisphere — gives a physically grounded, less noisy result. The
// trade-off is that we lose the easy contact-AO extension; that's why
// we keep ssao_PS around for users who prefer it.
//
// Approach per pixel:
//   1. Sample world-space normal + view-space Z from gbuf.
//   2. Pick a random azimuth direction in screen space (hash-jittered).
//   3. March 4 steps in each of (+dir, -dir) — 8 total horizon samples.
//      At each step, find the max angle that the local horizon makes
//      with the view ray.
//   4. Integrate cos(alpha) between the two horizon angles → occlusion.
//   5. (NEW Stage 31) Bent normal: bias the surface normal toward the
//      more-open horizon direction. The bias magnitude scales with how
//      asymmetric the two horizons are — when both are equally
//      occluded, bent N == N (no bias); when one side is much more
//      open, N tilts toward that side.
//
// Output:
//   .r   = AO factor (1.0 lit, 0.0 occluded)  — unchanged from before
//   .gba = bent normal × 0.5 + 0.5  (NEW)     — receiver decodes back
//
// pSsaoA is RGBA8 so the .gba channels were unused before this stage;
// no allocation change. 8-bit per component on a normal is ~0.4% loss,
// plenty for diffuse-IBL biasing (the receiver only uses bent N to
// lerp the sky-sample direction; small precision loss invisible).

sampler2D gbufTex  : register(s0);
sampler2D noiseTex : register(s1);

// .x = sample radius (world units)
// .y = depth bias
// .z = intensity scale
// .w = far clip
float4 gtaoParams : register(c10);

// .x = 1/width, .y = 1/height, .z = noise tile scale, .w = (unused)
float4 gtaoTexel : register(c11);

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

// NaN-safe normalize — see hdrResolve_PS.hlsl. GTAO normals get reused as
// horizon-cosine references; a NaN N makes one slice contribute a NaN
// contribution which becomes white through the saturate path, drilling a
// bright hole into the AO buffer.
float3 SafeNormalize(float3 v)
{
	float l2 = dot(v, v);
	return v * rsqrt(max(l2, 1e-8));
}

float fastAcos(float x)
{
	// 3-term polynomial approximation of acos — fits in budget on
	// ps_3_0 better than the intrinsic and is accurate enough for AO.
	float ax = abs(x);
	float r  = (-0.0187293 * ax + 0.0742610) * ax;
	r = (r - 0.2121144) * ax + 1.5707288;
	r = r * sqrt(1.0 - ax);
	return x < 0.0 ? 3.14159265 - r : r;
}

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float4 gbuf = tex2D(gbufTex, uv);
	float3 N = gbuf.rgb * 2.0 - 1.0;
	float  depth = gbuf.a;

	if(depth < 0.0001 || dot(N, N) < 0.01)
		return float4(1.0, 1.0, 1.0, 1.0);

	N = SafeNormalize(N);
	float farClip = gtaoParams.w;
	float centreZ = depth * farClip;

	// View-space ray direction at this pixel (approximate — assumes
	// the projection is roughly symmetric, which it is for the
	// camera-sized RT).
	float3 V = float3(0, 0, 1);	// we work in view-relative space here

	// Random azimuth from noise tile. The pixel-coords-driven hash
	// keeps the pattern stable across frames at the same uv.
	float3 rnd = tex2D(noiseTex, uv * gtaoTexel.z).xyz * 2.0 - 1.0;
	float angle = atan2(rnd.y, rnd.x);
	float2 dir = float2(cos(angle), sin(angle));

	float radius = gtaoParams.x;
	float bias   = gtaoParams.y;
	float intensity = gtaoParams.z;

	// Two-sided horizon integration. cos(h0) is the cosine of the
	// horizon angle going one way; cos(h1) is the other. Their
	// integral gives the visible cone size.
	float cosH0 = -1.0;	// horizon at extreme (no occlusion)
	float cosH1 = -1.0;

	[unroll]
	for(int i = 1; i <= 4; i++){
		float t = (float(i) / 4.0);
		float2 ofs = dir * radius * gtaoTexel.xy * 64.0 * t;
		float4 sA = tex2D(gbufTex, uv + ofs);
		float4 sB = tex2D(gbufTex, uv - ofs);
		float zA = sA.a * farClip;
		float zB = sB.a * farClip;

		// Horizon vectors in (offset, Z) space.
		float3 hA = float3(ofs * 64.0, centreZ - zA);
		float3 hB = float3(-ofs * 64.0, centreZ - zB);

		float lA = length(hA);
		float lB = length(hB);
		float cA = (lA > 1e-4) ? (hA.z / lA) : -1.0;
		float cB = (lB > 1e-4) ? (hB.z / lB) : -1.0;
		cosH0 = max(cosH0, cA - bias);
		cosH1 = max(cosH1, cB - bias);
	}

	// Visibility from horizon angles. The exact GTAO integral is
	// (sin(h0)^2 + sin(h1)^2) / 2 — we approximate with clamped cosines.
	float h0 = fastAcos(cosH0);
	float h1 = fastAcos(cosH1);
	float visibility = (sin(h0) * sin(h0) + sin(h1) * sin(h1)) * 0.5;
	float ao = 1.0 - saturate(1.0 - visibility) * intensity;

	// Bent normal (Stage 31). cosH0 = horizon along +dir, cosH1 = along
	// -dir. When cosH0 < cosH1 the +dir side is MORE OPEN (horizon
	// further from zenith ⇒ smaller cos ⇒ wider visible cone), so we
	// bend N toward +dir. The magnitude scales with the asymmetry. When
	// both horizons match (symmetric occlusion), bend = 0 and bentN = N.
	//
	// 0.5 multiplier keeps the bias subtle — receiver does the final
	// blend toward the bent direction at its own strength. The vertical
	// bend uses (1 - min(h0, h1)) so very-open scenes (low horizons)
	// keep bentN close to the original N; deep occlusion lets the bend
	// pull harder.
	float openDelta = cosH1 - cosH0;	// > 0 → +dir more open
	float openZ     = 1.0 - 0.5 * (cosH0 + cosH1);	// 0..1 verticality bias
	float3 bentN    = N + float3(dir * openDelta * 0.5, 0.0)
	                + float3(0.0, 0.0, openZ * 0.25);
	bentN           = SafeNormalize(bentN);

	// Pack bent normal × 0.5 + 0.5 into .gba so the receiver gets a
	// signed normal back via .gba * 2 - 1.
	float3 bentEnc  = bentN * 0.5 + 0.5;
	return float4(saturate(ao), bentEnc.x, bentEnc.y, bentEnc.z);
}
