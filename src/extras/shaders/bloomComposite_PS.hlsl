// Additive bloom composite with a soft "veil" curve that mimics film bloom
// without over-blowing highlights when the source already sits near white.

sampler2D tex0 : register(s0);
float4 bloomMix : register(c10); // .x = intensity, .y = saturation

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

float4 main(VS_out input) : COLOR
{
	float3 b = tex2D(tex0, input.TexCoord0).rgb;

	// Apply a mild saturation boost so coloured highlights (neon signs,
	// sodium street lamps) actually pop instead of bleaching to white.
	float lum = dot(b, float3(0.2126, 0.7152, 0.0722));
	b = lerp(float3(lum, lum, lum), b, bloomMix.y);

	// Soft veil — biases toward the brighter side of the curve.
	float3 veil = b / (b + 1.0);

	return float4(lerp(b, veil, 0.25) * bloomMix.x, 1.0);
}
