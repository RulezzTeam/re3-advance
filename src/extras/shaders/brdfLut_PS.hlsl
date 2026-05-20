// Split-sum BRDF LUT generator (Karis 2013).
//
// Bakes a 256×256 RG16F lookup texture once at CIBL startup. Each pixel
// stores the pre-integrated geometric × Fresnel terms of the GGX BRDF
// for a given (NdotV, roughness) pair, so the runtime IBL specular path
// becomes a single texture fetch instead of a per-pixel importance-sample
// loop. The split-sum approximation is:
//
//   IBL_specular(N,V,F0,r) ≈ prefiltered(R, r) * (F0 * LUT.r + LUT.g)
//
// where `prefiltered` is the GGX-prefiltered radiance cube (Stage 12 P2)
// and LUT.r/.g are the scale/bias outputs of this shader.
//
// Output:
//   .r = scale  for F0  (multiplies the surface F0 reflectance)
//   .g = bias   for F0  (adds to F0 for grazing-angle Fresnel)
//   .b/.a       unused; format is F16_RGBA for librw compatibility
//
// 32-sample Hammersley table — precomputed offline (Van der Corput in
// base 2 mirrored into [0,1]). 32 samples is enough for a smooth LUT;
// the table is hardcoded so the ps_3_0 loop stays simple and fits well
// inside the dynamic-loop instruction budget.

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

// GGX importance sample. Returns a half-vector H biased toward the
// surface normal at roughness=0 and uniformly hemispherical at r=1.
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
	return H;	// already in tangent space with N = (0,0,1)
}

// Smith's geometric term for GGX, split-form (NdotV side only — the
// LUT generator uses a different k than the runtime BRDF, hence the
// dedicated path).
float GeometrySchlickGGX(float NdotV, float roughness)
{
	float a = roughness;
	float k = (a * a) / 2.0;
	float denom = NdotV * (1.0 - k) + k;
	return NdotV / denom;
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
	return GeometrySchlickGGX(NdotV, roughness) *
	       GeometrySchlickGGX(NdotL, roughness);
}

float2 IntegrateBRDF(float NdotV, float roughness)
{
	// V vector reconstructed from NdotV (φ = 0, in the xz half-plane).
	float3 V;
	V.x = sqrt(1.0 - NdotV * NdotV);
	V.y = 0.0;
	V.z = NdotV;

	float A = 0.0;
	float B = 0.0;

	[loop]
	for(int i = 0; i < 32; i++){
		float2 Xi = kHammersley32[i];
		float3 H = ImportanceSampleGGX(Xi, roughness);
		// L = reflect V about H in tangent space with N=(0,0,1).
		float3 L = 2.0 * dot(V, H) * H - V;

		float NdotL = max(L.z, 0.0);
		float NdotH = max(H.z, 0.0);
		float VdotH = max(dot(V, H), 0.0);

		if(NdotL > 0.0){
			float G = GeometrySmith(NdotV, NdotL, roughness);
			// G_Vis = G × (V·H) / (N·H × N·V) — the visibility term
			// the split-sum factors out from the Fresnel-free integral.
			float G_Vis = (G * VdotH) / max(NdotH * NdotV, 1e-5);
			float Fc = pow(1.0 - VdotH, 5.0);
			A += (1.0 - Fc) * G_Vis;
			B += Fc * G_Vis;
		}
	}
	return float2(A, B) / 32.0;
}

float4 main(in float2 uv : TEXCOORD0) : COLOR0
{
	// uv.x = NdotV (0 = grazing, 1 = head-on)
	// uv.y = roughness (0 = mirror, 1 = fully diffuse)
	// Clamp the inputs to keep the integration well-defined at the
	// extreme corners (NdotV = 0 gives a divide-by-zero in G_Vis).
	float NdotV    = max(uv.x, 0.001);
	float rough    = max(uv.y, 0.001);
	float2 brdf    = IntegrateBRDF(NdotV, rough);
	return float4(brdf.x, brdf.y, 0.0, 1.0);
}
