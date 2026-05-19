// HDR -> LDR resolve shader with optional SSAO compose.
//
// Reads the RGBA16F off-screen scene RT (CGBuffer::pHdrScene), optionally
// multiplies by an SSAO mask (CPostFX::pSsaoA), then applies the final
// tonemap pipeline (exposure -> ACES -> saturation -> gamma) before writing
// to the LDR backbuffer.

sampler2D hdrTex  : register(s0);
sampler2D ssaoTex : register(s1);	// R8 AO; 1.0 = fully lit

// .x = exposure (linear multiplier, 1.0 = neutral)
// .y = ACES toggle (0..1, lerps toward filmic curve)
// .z = gamma toggle (0..1)
// .w = saturation (1.0 = neutral)
float4 hdrTonemap : register(c10);

// .x = SSAO strength (0 = disabled), .y = AO power curve, .zw = unused
float4 hdrSsao : register(c11);

float3 ACES(float3 x)
{
	return saturate((x*(2.51*x + 0.03)) / (x*(2.43*x + 0.59) + 0.14));
}

float4 main(in float2 uv : TEXCOORD0) : COLOR0
{
	float3 col = tex2D(hdrTex, uv).rgb;

	// SSAO compose — multiply ambient term by AO. Skipped when strength = 0
	// so the shader works correctly even when SSAO RTs are stale/disabled.
	[branch]
	if(hdrSsao.x > 0.001){
		float ao = tex2D(ssaoTex, uv).r;
		ao = pow(saturate(ao), max(hdrSsao.y, 0.1));
		ao = lerp(1.0, ao, hdrSsao.x);
		col *= ao;
	}

	// Exposure (linear).
	col *= hdrTonemap.x;

	// ACES filmic tonemap, blendable.
	float3 aces = ACES(col);
	col = lerp(col, aces, hdrTonemap.y);

	// Saturation in luminance space.
	float lum = dot(col, float3(0.2126, 0.7152, 0.0722));
	col = lerp(float3(lum, lum, lum), col, hdrTonemap.w);

	// Gamma 2.2 encode for the LDR backbuffer.
	float3 gam = pow(max(col, 1e-5), 1.0/2.2);
	col = lerp(col, gam, hdrTonemap.z);

	return float4(saturate(col), 1.0);
}
