// Bilateral SSAO + Bent Normal blur — preserves edges by weighting taps
// by depth similarity (avoids the gray halo on building corners +
// foreground bushes).
//
// 7-tap separable kernel; called twice (H then V) for ~13-tap effective
// reach with linear-in-N cost.
//
// Channel layout (Stage 31):
//   .r   = AO factor
//   .gba = bent normal × 0.5 + 0.5 (packed in 0..1)
// All four channels share the same bilateral depth weights so the bent
// normal stays edge-aware too (without this, the blur would average
// the bent direction across building silhouettes and re-introduce the
// "light bleed" we're trying to fix).

sampler2D aoTex    : register(s0);	// previous-pass AO + bent N (RGBA8)
sampler2D gbufTex  : register(s1);	// normal+depth (for bilateral weights)

// .xy = direction * texelSize, .z = depth-sigma (smaller = sharper edges),
// .w  = farClip
float4 blurParams : register(c10);

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float centreZ = tex2D(gbufTex, uv).a * blurParams.w;

	const float kWeights[4] = { 0.227027, 0.194595, 0.121622, 0.054054 };
	float4 centreSample = tex2D(aoTex, uv);
	float4 sum = centreSample * kWeights[0];
	float  sumW = kWeights[0];

	[unroll(3)]
	for(int i = 1; i < 4; i++){
		float2 o = blurParams.xy * (float)i;

		float zP = tex2D(gbufTex, uv + o).a * blurParams.w;
		float zN = tex2D(gbufTex, uv - o).a * blurParams.w;
		float wP = kWeights[i] * exp(-abs(zP - centreZ) * blurParams.z);
		float wN = kWeights[i] * exp(-abs(zN - centreZ) * blurParams.z);
		sum  += tex2D(aoTex, uv + o) * wP;
		sum  += tex2D(aoTex, uv - o) * wN;
		sumW += wP + wN;
	}

	// Divide all 4 channels by sumW — preserves AO + bent normal both.
	// The bent normal direction stays approximately unit after the blur
	// because bilateral weights are small for edge-discontinuous taps;
	// any residual length drift is fixed downstream when the receiver
	// renormalises after decoding (.gba * 2 - 1 → normalize).
	return sum / sumW;
}
