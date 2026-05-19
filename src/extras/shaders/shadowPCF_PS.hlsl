// 16-tap Poisson disk PCF for blob shadow decals.
//
// Compared to the 9-tap Gaussian box this gives a much wider effective
// penumbra (no visible grid pattern) at only ~7 extra samples, well within
// the ps_3_0 budget. The rotation hash makes the dithering temporally
// coherent enough to hide on a 60 Hz monitor without TAA.
//
// Inputs:
//   tex0      — original shadow blob
//   c10.xy    — 1/textureWidth, 1/textureHeight
//   c10.z     — pcf radius (default ~1.4)
//   c10.w     — unused (reserved for future use)

sampler2D tex0 : register(s0);
float4 pcfParams : register(c10);

struct VS_out {
	float4 Position		: POSITION;
	float3 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

static const float2 kPoissonDisk[16] = {
	float2( -0.94201624, -0.39906216 ),
	float2(  0.94558609, -0.76890725 ),
	float2( -0.09418410, -0.92938870 ),
	float2(  0.34495938,  0.29387760 ),
	float2( -0.91588581,  0.45771432 ),
	float2( -0.81544232, -0.87912464 ),
	float2( -0.38277543,  0.27676845 ),
	float2(  0.97484398,  0.75648379 ),
	float2(  0.44323325, -0.97511554 ),
	float2(  0.53742981, -0.47373420 ),
	float2( -0.26496911, -0.41893023 ),
	float2(  0.79197514,  0.19090188 ),
	float2( -0.24188840,  0.99706507 ),
	float2( -0.81409955,  0.91437590 ),
	float2(  0.19984126,  0.78641367 ),
	float2(  0.14383161, -0.14100790 )
};

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0.xy;
	float2 stride = pcfParams.xy * max(pcfParams.z, 0.5);

	// Per-pixel rotation so neighbouring fragments sample different rings.
	float rot = frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
	float c = cos(rot), s = sin(rot);
	float2x2 R = float2x2(c, -s, s, c);

	float4 acc = float4(0,0,0,0);
	[unroll(16)]
	for(int i = 0; i < 16; i++){
		float2 off = mul(R, kPoissonDisk[i]) * stride;
		acc += tex2D(tex0, uv + off);
	}
	acc *= 1.0/16.0;

	return input.Color * acc;
}
