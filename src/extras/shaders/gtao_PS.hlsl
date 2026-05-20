// Ground-Truth Ambient Occlusion (GTAO).
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
//
// Output: AO in R, 1.0 = lit, 0.0 = occluded. Same format as ssao_PS so
// the bilateral blur pass can be reused.

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

	N = normalize(N);
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
	return float4(saturate(ao), 1.0, 1.0, 1.0);
}
