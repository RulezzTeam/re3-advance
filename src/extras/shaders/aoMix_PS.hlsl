// AO Mixer — combines 3 separately-rendered AO buffers (SSAO + GTAO +
// HBAO) into one. Each algorithm catches different occlusion features:
//   - SSAO (Crytek hemisphere) — solid medium-scale concavity
//   - GTAO — physically correct horizon integration
//   - HBAO — sharp edge contacts
// Multiplying them like AO masks gives the union of all three, which
// is the visually richest result. We take min() instead of × to avoid
// overdarkening.
//
// Bindings:
//   s0 = pSsaoA  — SSAO output (R = ao)
//   s1 = pSsaoB  — GTAO output
//   s2 = pSsaoMixC — HBAO output
//
// mixParams.x = SSAO weight, .y = GTAO weight, .z = HBAO weight,
// mixParams.w = mode (0 = min, 1 = weighted average, 2 = multiply)

sampler2D ssaoTex : register(s0);
sampler2D gtaoTex : register(s1);
sampler2D hbaoTex : register(s2);

float4 mixParams : register(c10);

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float a = tex2D(ssaoTex, uv).r;
	float b = tex2D(gtaoTex, uv).r;
	float c = tex2D(hbaoTex, uv).r;

	float r;
	if(mixParams.w < 0.5){
		// Min mode — darkest wins. Aggressive.
		r = min(min(a, b), c);
	}else if(mixParams.w < 1.5){
		// Weighted average.
		float wA = mixParams.x;
		float wB = mixParams.y;
		float wC = mixParams.z;
		float sum = max(wA + wB + wC, 1e-4);
		r = (a * wA + b * wB + c * wC) / sum;
	}else{
		// Multiply mode — most natural compose if each algorithm is
		// well-tuned. Be careful: produces very dark output if any of
		// the inputs go close to 0.
		r = a * b * c;
		// Pull back toward less-dark range.
		r = pow(r, 0.7);
	}

	return float4(saturate(r), 1.0, 1.0, 1.0);
}
