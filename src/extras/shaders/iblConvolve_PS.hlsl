// Irradiance cubemap convolution.
//
// Reads a source environment cubemap (e.g. captured sky) and outputs
// the diffuse irradiance: the cosine-weighted integral over the
// upper hemisphere oriented along the destination face normal.
//
// The shader runs once per face of the destination cube. The host
// passes the face's right/up/forward basis as constants + a sample
// count. We use a coarse hemisphere kernel (32 samples in concentric
// rings) which is overkill for the gentle 32² destination but well
// below the ps_3_0 instruction budget.

samplerCUBE srcCube : register(s0);

// .xyz = face forward direction (cube face normal), .w = unused
float4 faceForward : register(c10);
// .xyz = face right (tangent), .w = unused
float4 faceRight   : register(c11);
// .xyz = face up (bitangent), .w = unused
float4 faceUp      : register(c12);

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

float4 main(VS_out input) : COLOR
{
	// Reconstruct the hemisphere normal for this destination texel.
	// uv ∈ [0,1] → face-space coords [-1, 1]; mapped through R/U/F.
	float2 fc = input.TexCoord0 * 2.0 - 1.0;
	float3 N = normalize(faceForward.xyz + fc.x * faceRight.xyz + fc.y * faceUp.xyz);

	// Build a tangent basis around N for hemisphere sampling.
	float3 up = abs(N.z) < 0.99 ? float3(0,0,1) : float3(1,0,0);
	float3 T = normalize(cross(up, N));
	float3 B = cross(N, T);

	// 32 hemisphere samples — 4 inclination rings × 8 azimuth steps.
	// Cosine-weighted ⇒ Lambertian diffuse approximation.
	const int NSAMPLES = 32;
	const float TWO_PI = 6.2831853;
	float3 acc = float3(0,0,0);
	float wAcc = 0.0;

	[unroll]
	for(int i = 0; i < 32; i++){
		// Hammersley-ish stratified sample (golden-ratio low-discrepancy).
		float u = (float(i) + 0.5) / float(NSAMPLES);
		float v = frac(float(i) * 0.61803398875);
		float phi = TWO_PI * u;
		float cosTheta = 1.0 - v;	// uniform on hemisphere
		float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
		float3 dir = T * (cos(phi) * sinTheta)
		           + B * (sin(phi) * sinTheta)
		           + N * cosTheta;
		// Cosine weight: dot(N, dir) = cosTheta.
		float3 s = texCUBE(srcCube, dir).rgb;
		acc += s * cosTheta;
		wAcc += cosTheta;
	}

	return float4(acc / max(wAcc, 1e-4), 1.0);
}
