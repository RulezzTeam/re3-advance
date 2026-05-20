// GGX-prefiltered specular environment map generator (Karis split-sum
// approximation, Stage 12 P2).
//
// One pass per (face, mip) of the destination prefilterCube. The shader
// samples the source captureCube along an importance-sampled GGX half-
// vector distribution at the mip's target roughness, then averages the
// contribution weighted by NdotL. The result is a roughness-blurred
// version of the source — mip 0 = sharp (roughness 0), mip N = fully
// rough (roughness 1).
//
// Pair with the BRDF LUT (brdfLut_PS) and you get the full split-sum
// approximation in the receiver:
//   IBL_specular(N,V,F0,r) ≈ texCUBElod(prefilter, R, r * MAX_MIP)
//                          * (F0 * LUT.r + LUT.g)
//
// Inputs:
//   s0           = source captureCube (sharp HDR sky+sun)
//   c10.x        = target roughness for this mip (0..1)
//   c10.y        = source cube resolution (for the sampling rate hack)
//   c11..c13     = face basis vectors (forward, right, up) — same as
//                  iblSkyToCube_PS uses to reconstruct per-face world
//                  directions from the UV inside the face.

samplerCUBE srcCube : register(s0);
float4 prefilterParams : register(c10);	// .x = roughness, .y = src res
float4 faceForward     : register(c11);
float4 faceRight       : register(c12);
float4 faceUp          : register(c13);

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
};

// Same 32-tap Hammersley table as the BRDF LUT generator — avoids
// recomputing per-pass, fits in the ps_3_0 constant table comfortably.
static const float2 kHammersley32[32] = {
	float2(0.000000, 0.000000), float2(0.031250, 0.500000),
	float2(0.062500, 0.250000), float2(0.093750, 0.750000),
	float2(0.125000, 0.125000), float2(0.156250, 0.625000),
	float2(0.187500, 0.375000), float2(0.218750, 0.875000),
	float2(0.250000, 0.062500), float2(0.281250, 0.562500),
	float2(0.312500, 0.312500), float2(0.343750, 0.812500),
	float2(0.375000, 0.187500), float2(0.406250, 0.687500),
	float2(0.437500, 0.437500), float2(0.468750, 0.937500),
	float2(0.500000, 0.031250), float2(0.531250, 0.531250),
	float2(0.562500, 0.281250), float2(0.593750, 0.781250),
	float2(0.625000, 0.156250), float2(0.656250, 0.656250),
	float2(0.687500, 0.406250), float2(0.718750, 0.906250),
	float2(0.750000, 0.093750), float2(0.781250, 0.593750),
	float2(0.812500, 0.343750), float2(0.843750, 0.843750),
	float2(0.875000, 0.218750), float2(0.906250, 0.718750),
	float2(0.937500, 0.468750), float2(0.968750, 0.968750),
};

// GGX importance sample — half-vector in tangent space with N = (0,0,1).
float3 ImportanceSampleGGX(float2 Xi, float roughness)
{
	float a = roughness * roughness;
	float phi = 2.0 * 3.14159265 * Xi.x;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	float3 H;
	H.x = cos(phi) * sinTheta;
	H.y = sin(phi) * sinTheta;
	H.z = cosTheta;
	return H;
}

float4 main(in float2 uv : TEXCOORD0) : COLOR0
{
	// Reconstruct the world-space direction for this pixel of the face.
	// uv is 0..1 across the face; the face basis maps it to a world dir.
	float2 face = uv * 2.0 - 1.0;
	float3 N = normalize(faceForward.xyz
	                     + faceRight.xyz * face.x
	                     + faceUp.xyz    * face.y);

	// In the Karis approximation, V = R = N (camera-aligned reflection).
	// This drops the dependence on view direction inside the prefilter
	// and trades a bit of accuracy for a single closed-form texture.
	float3 R = N;
	float3 V = N;
	float roughness = prefilterParams.x;

	// Build local tangent frame around N so the tangent-space H from
	// ImportanceSampleGGX can be rotated into world space.
	float3 up = abs(N.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
	float3 tangent = normalize(cross(up, N));
	float3 bitangent = cross(N, tangent);

	float3 acc = float3(0, 0, 0);
	float  wAcc = 0.0;

	[loop]
	for(int i = 0; i < 32; i++){
		float2 Xi = kHammersley32[i];
		float3 Hloc = ImportanceSampleGGX(Xi, roughness);
		float3 H = tangent * Hloc.x + bitangent * Hloc.y + N * Hloc.z;
		float3 L = 2.0 * dot(V, H) * H - V;
		float NdotL = saturate(dot(N, L));
		if(NdotL > 0.0){
			// Weighted by NdotL so the integration matches the actual
			// hemispherical projection. The classic Karis pre-filter
			// adds a Hammersley-rate term to bias toward higher-prob
			// directions; with only 32 samples the simpler NdotL
			// weighting reads cleaner.
			acc  += texCUBE(srcCube, L).rgb * NdotL;
			wAcc += NdotL;
		}
	}

	float3 prefiltered = (wAcc > 0.0) ? acc / wAcc : float3(0, 0, 0);
	return float4(prefiltered, 1.0);
}
