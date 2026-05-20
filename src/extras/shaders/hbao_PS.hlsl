// Horizon-Based Ambient Occlusion (HBAO).
//
// Walks 4 azimuth directions × 6 step samples per direction. For each
// direction the maximum horizon angle is found, then the integrated
// occlusion is the difference between the tangent at the centre pixel
// (using the per-pixel normal) and the horizon angle. This is the
// classic NVIDIA HBAO+ approach distilled to ps_3_0.
//
// Inputs: same as ssao_PS / gtao_PS — gbuf at s0, noise tile at s1.
// Constants reuse the same c10/c11 layout so the host doesn't need a
// separate dispatch.

sampler2D gbufTex  : register(s0);
sampler2D noiseTex : register(s1);

float4 hbaoParams : register(c10);	// .x radius, .y bias, .z intensity, .w farClip
float4 hbaoTexel  : register(c11);	// .xy = 1/W,1/H, .z = noise scale

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

float ComputeAOSlice(float2 uv, float2 dir, float3 N, float centreZ, float farClip, float radius, float bias)
{
	// March 6 steps along `dir`, tracking the maximum horizon angle.
	// Return per-slice AO contribution: max(0, sin(horizon) - sin(tangent)).
	float maxHoriz = -1.0;	// max sin(theta) so far
	float tangent  = dot(N, float3(dir, 0));
	tangent = clamp(tangent, -0.99, 0.99);
	float sinTangent = tangent;

	[unroll]
	for(int i = 1; i <= 6; i++){
		float t = float(i) / 6.0;
		float2 ofs = dir * radius * hbaoTexel.xy * 80.0 * t;
		float4 sG  = tex2D(gbufTex, uv + ofs);
		if(sG.a < 0.0001) continue;
		float sZ = sG.a * farClip;
		// Horizon vector in screen-space: (offset world distance, Δz).
		// sin(horizon) ≈ Δz / |horizon vector|.
		float dz = centreZ - sZ;
		float worldOfs = radius * t;
		float len = sqrt(worldOfs * worldOfs + dz * dz);
		float sinH = (len > 1e-4) ? (dz / len) : -1.0;
		// Bias to reject very-close hits (anti-self-occlusion).
		sinH -= bias;
		maxHoriz = max(maxHoriz, sinH);
	}

	float contribution = max(0.0, maxHoriz - sinTangent);
	return contribution;
}

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float4 gbuf = tex2D(gbufTex, uv);
	float3 N = gbuf.rgb * 2.0 - 1.0;
	float depth = gbuf.a;

	if(depth < 0.0001 || dot(N, N) < 0.01)
		return float4(1.0, 1.0, 1.0, 1.0);

	N = normalize(N);
	float farClip = hbaoParams.w;
	float centreZ = depth * farClip;

	float radius = hbaoParams.x;
	float bias   = hbaoParams.y;
	float intensity = hbaoParams.z;

	// Per-pixel random rotation from the noise tile.
	float3 rnd = tex2D(noiseTex, uv * hbaoTexel.z).xyz * 2.0 - 1.0;
	float baseAngle = atan2(rnd.y, rnd.x);

	// 4 azimuth slices, each rotated by 45° from baseAngle.
	float occ = 0.0;
	const float invPi4 = 0.7853981;	// pi/4
	[unroll]
	for(int s = 0; s < 4; s++){
		float a = baseAngle + invPi4 * float(s);
		float2 d = float2(cos(a), sin(a));
		occ += ComputeAOSlice(uv, d, N, centreZ, farClip, radius, bias);
	}
	occ /= 4.0;
	float ao = 1.0 - saturate(occ) * intensity;
	return float4(saturate(ao), 1.0, 1.0, 1.0);
}
