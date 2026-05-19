// Final-pass colour grading and tonemap for the LDR pipeline.
//
// Pipeline order:
//   1. Chromatic aberration prefetch (radial RGB split).
//   2. Legacy blur-colour overlay (iterative additive blend, unchanged).
//   3. Exposure scaling                    (tonemapParams.z)
//   4. ACES filmic tonemap                  (tonemapParams.x)
//   5. Saturation in luminance space        (tonemapParams.w)
//   6. Vignette darkening                   (vignetteParams.*)
//   7. Gamma 2.2 correction                 (tonemapParams.y)
//
// Most stages have a toggle/scale so the host can drive them from the
// DebugMenu or leave them at neutral values to keep legacy behaviour.

sampler2D tex : register(s0);

float4 blurcol         : register(c10); // legacy blur colour (.a = strength)
float4 tonemapParams   : register(c11); // .x aces, .y gamma, .z exposure, .w saturation
float4 vignetteParams  : register(c12); // .x intensity, .y softness, .z roundness, .w aspect
float4 caParams        : register(c13); // .x strength, .y scale-by-distance, .zw unused

float3 ACES(float3 x)
{
	return saturate((x*(2.51*x + 0.03)) / (x*(2.43*x + 0.59) + 0.14));
}

float Vignette(float2 uv, float intensity, float softness, float roundness, float aspect)
{
	float2 c = uv - float2(0.5, 0.5);
	c.x *= aspect * roundness;
	float dist = length(c) * 1.4142136;
	float v = smoothstep(1.0 - softness, 1.0 + softness, dist);
	return saturate(1.0 - intensity * v);
}

float3 ChromaticAberration(sampler2D s, float2 uv, float strength, float distScale)
{
	float2 dir = uv - float2(0.5, 0.5);
	float dist = length(dir);
	float scale = strength * lerp(1.0, dist * 2.0, distScale);
	float2 off = dir * scale;
	float r = tex2D(s, uv + off).r;
	float g = tex2D(s, uv          ).g;
	float b = tex2D(s, uv - off    ).b;
	return float3(r, g, b);
}

float4 main(in float2 uv : TEXCOORD0) : COLOR0
{
	// --- 1. Chromatic aberration (skipped when strength == 0) ---------
	float4 src;
	[branch]
	if(caParams.x > 0.0001){
		src.rgb = ChromaticAberration(tex, uv, caParams.x, caParams.y);
		src.a = tex2D(tex, uv).a;
	}else{
		src = tex2D(tex, uv);
	}

	// --- 2. Legacy blur colour overlay --------------------------------
	float a = blurcol.a;
	float4 doublec = saturate(blurcol * 2);
	float4 prev = src;
	[unroll(5)]
	for(int i = 0; i < 5; i++){
		float4 tmp = src*(1 - a) + prev*doublec*a;
		tmp += prev*blurcol;
		tmp += prev*blurcol;
		prev = saturate(tmp);
	}
	float3 col = prev.rgb;

	// --- 3. Exposure --------------------------------------------------
	col *= tonemapParams.z;

	// --- 4. ACES tonemap ---------------------------------------------
	float3 aces = ACES(col);
	col = lerp(col, aces, tonemapParams.x);

	// --- 5. Saturation -----------------------------------------------
	float lum = dot(col, float3(0.2126, 0.7152, 0.0722));
	col = lerp(float3(lum, lum, lum), col, tonemapParams.w);

	// --- 6. Vignette -------------------------------------------------
	col *= Vignette(uv,
	                vignetteParams.x,
	                vignetteParams.y,
	                vignetteParams.z,
	                vignetteParams.w);

	// --- 7. Gamma 2.2 ------------------------------------------------
	float3 gammaCorrected = pow(max(col, 1e-5), 1.0/2.2);
	col = lerp(col, gammaCorrected, tonemapParams.y);

	return float4(col, 1.0);
}
