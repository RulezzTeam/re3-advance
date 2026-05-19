// Tonemap-aware bright-pass filter for bloom.
//
// Improvements over the original 1-tap implementation:
//   - 5-tap box pre-filter to suppress sub-pixel flicker on bright edges.
//   - Karis average over the sampled luminances (reduces fireflies when
//     a single very bright pixel sneaks into the brightpass).
//   - Soft knee in luminance space using a quadratic falloff.

sampler2D tex0 : register(s0);
float4 bloomParams : register(c10); // .x = threshold, .y = knee, .z = intensity, .w = unused
float4 brightTexel : register(c11); // .xy = 1/srcWidth, 1/srcHeight (for prefilter)

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

float Luma(float3 c)
{
	return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float3 KarisAvg(float3 a, float3 b, float3 c, float3 d, float3 e)
{
	// Inverse-luminance weighted average — bright outliers contribute less.
	float wa = 1.0 / (1.0 + Luma(a));
	float wb = 1.0 / (1.0 + Luma(b));
	float wc = 1.0 / (1.0 + Luma(c));
	float wd = 1.0 / (1.0 + Luma(d));
	float we = 1.0 / (1.0 + Luma(e));
	float wsum = wa + wb + wc + wd + we;
	return (a*wa + b*wb + c*wc + d*wd + e*we) / wsum;
}

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float2 o = brightTexel.xy;

	// 5-tap box prefilter (centre + 4 cardinals) — cheap anti-flicker.
	float3 c0 = tex2D(tex0, uv).rgb;
	float3 c1 = tex2D(tex0, uv + float2( o.x,  0.0)).rgb;
	float3 c2 = tex2D(tex0, uv + float2(-o.x,  0.0)).rgb;
	float3 c3 = tex2D(tex0, uv + float2( 0.0,  o.y)).rgb;
	float3 c4 = tex2D(tex0, uv + float2( 0.0, -o.y)).rgb;
	float3 col = KarisAvg(c0, c1, c2, c3, c4);

	// Soft-knee thresholding (Unreal-style)
	float l = Luma(col);
	float knee = max(bloomParams.y, 0.0001);
	float soft = clamp(l - bloomParams.x + knee, 0.0, 2.0*knee);
	soft = soft * soft / (4.0 * knee + 0.0001);
	float weight = max(l - bloomParams.x, soft);
	float scale = weight / max(l, 0.0001);

	return float4(col * scale * bloomParams.z, 1.0);
}
