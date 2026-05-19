// HDR -> LDR resolve shader.
//
// Reads the RGBA16F off-screen scene RT (CGBuffer::pHdrScene), applies the
// final tonemap pipeline (exposure -> ACES -> saturation -> gamma) without
// the iterative blur-colour loop or vignette/CA used by colourfilterVC_PS,
// and writes the result into the LDR backbuffer.
//
// We keep this pass separate from colourfilterVC_PS because the legacy
// colour-filter shader applies an LDR clamp inside its blur loop — that
// would destroy >1 HDR values before they hit ACES. Splitting the pass
// gives us a clean, no-clamp tonemap for the HDR pipeline and leaves the
// existing LDR colour-filter intact for legacy mode.

sampler2D hdrTex : register(s0);

// .x = exposure (linear multiplier, 1.0 = neutral)
// .y = ACES toggle (0..1, lerps toward filmic curve)
// .z = gamma toggle (0..1)
// .w = saturation (1.0 = neutral)
float4 hdrTonemap : register(c10);

float3 ACES(float3 x)
{
	return saturate((x*(2.51*x + 0.03)) / (x*(2.43*x + 0.59) + 0.14));
}

float4 main(in float2 uv : TEXCOORD0) : COLOR0
{
	float3 col = tex2D(hdrTex, uv).rgb;

	// Exposure (linear). Default 1.0 keeps the scene at "stock" brightness.
	col *= hdrTonemap.x;

	// ACES filmic tonemap, blendable to verify HDR input visually.
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
