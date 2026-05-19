// Separable Gaussian blur tuned for bloom.
//
// 13-tap effective kernel (sigma ~= 2.5) realised with 7 bilinear samples
// using the linear-filtering trick. The host passes the per-pass direction
// in c10.xy (= rcp(width|height) along the chosen axis, the other
// component zero). This shader stays well under the ps_3_0 ALU budget so
// we can call it twice per frame without measurable cost on modern GPUs.

sampler2D tex0  : register(s0);
float4 blurDir  : register(c10); // .xy = direction * texelSize

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

float4 main(VS_out input) : COLOR
{
	// 13-tap Gaussian collapsed to 7 bilinear samples.
	// Pre-computed from the linear-filtering identity:
	//   w_i = w(a) + w(b),  off_i = (w(a)*off(a) + w(b)*off(b)) / w_i
	// with sigma=2.5 raw weights w(0..6).
	const float w0 = 0.196482;
	const float w1 = 0.296907; const float o1 = 1.411765;
	const float w2 = 0.094470; const float o2 = 3.294118;
	const float w3 = 0.010381; const float o3 = 5.176471;

	float2 uv = input.TexCoord0;
	float2 d  = blurDir.xy;

	float3 c  = tex2D(tex0, uv).rgb * w0;
	c += tex2D(tex0, uv + d * o1).rgb * w1;
	c += tex2D(tex0, uv - d * o1).rgb * w1;
	c += tex2D(tex0, uv + d * o2).rgb * w2;
	c += tex2D(tex0, uv - d * o2).rgb * w2;
	c += tex2D(tex0, uv + d * o3).rgb * w3;
	c += tex2D(tex0, uv - d * o3).rgb * w3;

	return float4(c, 1.0);
}
