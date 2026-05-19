// Bilateral SSAO blur — preserves edges by weighting taps by depth
// similarity (avoids the gray halo on building corners + foreground bushes).
//
// 7-tap separable kernel; called twice (H then V) for ~13-tap effective
// reach with linear-in-N cost.

sampler2D aoTex    : register(s0);	// previous-pass AO (R8)
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
	float sumAO = tex2D(aoTex, uv).r * kWeights[0];
	float sumW  = kWeights[0];

	[unroll(3)]
	for(int i = 1; i < 4; i++){
		float2 o = blurParams.xy * (float)i;

		float zP = tex2D(gbufTex, uv + o).a * blurParams.w;
		float zN = tex2D(gbufTex, uv - o).a * blurParams.w;
		float wP = kWeights[i] * exp(-abs(zP - centreZ) * blurParams.z);
		float wN = kWeights[i] * exp(-abs(zN - centreZ) * blurParams.z);
		sumAO += tex2D(aoTex, uv + o).r * wP;
		sumAO += tex2D(aoTex, uv - o).r * wN;
		sumW  += wP + wN;
	}

	return float4(sumAO / sumW, 1.0, 1.0, 1.0);
}
