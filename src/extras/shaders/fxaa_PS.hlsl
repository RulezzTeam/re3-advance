// FXAA 3.11 Quality preset — full edge search + subpixel offset.
//
// Compared to the Console preset, this version:
//   * Detects edge orientation from a 3x3 neighbourhood gradient instead of
//     a diagonal hint.
//   * Walks the edge in both directions (up to 8 steps each) to find its
//     end, giving accurate alignment on long contours.
//   * Adds a subpixel offset term that smooths near-1-pixel features
//     (sharp text, fine railings).
//
// The shader fits comfortably in ps_3_0; no [unroll(N)] gymnastics needed.

sampler2D tex0  : register(s0);
float4 fxaaParams : register(c10);
// .x, .y = 1/sourceWidth, 1/sourceHeight
// .z     = subpix amount   (0..1, ~0.75 default)
// .w     = edge threshold  (~0.166 default)

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

float Luma(float3 rgb)
{
	return dot(rgb, float3(0.299, 0.587, 0.114));
}

float4 main(VS_out input) : COLOR
{
	float2 uv  = input.TexCoord0;
	float2 rcp = fxaaParams.xy;
	float  subpix         = fxaaParams.z;
	float  edgeThreshold  = max(fxaaParams.w, 0.063);
	float  edgeThresholdMin = 0.0312;

	// Neighbours
	float3 rgbM = tex2D(tex0, uv).rgb;
	float lumaM = Luma(rgbM);
	float lumaN = Luma(tex2D(tex0, uv + float2(0.0,    -rcp.y)).rgb);
	float lumaS = Luma(tex2D(tex0, uv + float2(0.0,     rcp.y)).rgb);
	float lumaE = Luma(tex2D(tex0, uv + float2( rcp.x,  0.0  )).rgb);
	float lumaW = Luma(tex2D(tex0, uv + float2(-rcp.x,  0.0  )).rgb);

	float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
	float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
	float range   = lumaMax - lumaMin;

	// Flat region — early out.
	if(range < max(edgeThresholdMin, lumaMax * edgeThreshold))
		return float4(rgbM, 1.0);

	// Diagonals (used for orientation + subpix term)
	float lumaNW = Luma(tex2D(tex0, uv + float2(-rcp.x, -rcp.y)).rgb);
	float lumaNE = Luma(tex2D(tex0, uv + float2( rcp.x, -rcp.y)).rgb);
	float lumaSW = Luma(tex2D(tex0, uv + float2(-rcp.x,  rcp.y)).rgb);
	float lumaSE = Luma(tex2D(tex0, uv + float2( rcp.x,  rcp.y)).rgb);

	// Edge orientation via centred 2nd-order gradient.
	float edgeH = abs(lumaNW + lumaNE - 2.0*lumaN)
	            + 2.0*abs(lumaW + lumaE - 2.0*lumaM)
	            + abs(lumaSW + lumaSE - 2.0*lumaS);
	float edgeV = abs(lumaNW + lumaSW - 2.0*lumaW)
	            + 2.0*abs(lumaN + lumaS - 2.0*lumaM)
	            + abs(lumaNE + lumaSE - 2.0*lumaE);
	bool horz = edgeH >= edgeV;

	float stepSize = horz ? rcp.y : rcp.x;
	float luma1 = horz ? lumaN : lumaW;
	float luma2 = horz ? lumaS : lumaE;

	float grad1 = luma1 - lumaM;
	float grad2 = luma2 - lumaM;
	bool is1Steepest = abs(grad1) >= abs(grad2);
	float gradientScaled = 0.25 * max(abs(grad1), abs(grad2));

	float stepLength = is1Steepest ? -stepSize : stepSize;
	float lumaLocalAvg = 0.5 * (is1Steepest ? luma1 + lumaM : luma2 + lumaM);

	float2 currentUv = uv;
	if(horz) currentUv.y += stepLength * 0.5;
	else     currentUv.x += stepLength * 0.5;

	float2 offset = horz ? float2(rcp.x, 0.0) : float2(0.0, rcp.y);

	// 8-iter edge walk in both directions.
	float2 uv1 = currentUv - offset;
	float2 uv2 = currentUv + offset;
	float  lumaEnd1 = 0.0, lumaEnd2 = 0.0;
	bool   reached1 = false, reached2 = false;

	[unroll(8)]
	for(int i = 0; i < 8; i++){
		if(!reached1){
			lumaEnd1 = Luma(tex2D(tex0, uv1).rgb) - lumaLocalAvg;
			if(abs(lumaEnd1) >= gradientScaled) reached1 = true;
			else                                uv1 -= offset;
		}
		if(!reached2){
			lumaEnd2 = Luma(tex2D(tex0, uv2).rgb) - lumaLocalAvg;
			if(abs(lumaEnd2) >= gradientScaled) reached2 = true;
			else                                uv2 += offset;
		}
	}

	float distance1 = horz ? (uv.x - uv1.x) : (uv.y - uv1.y);
	float distance2 = horz ? (uv2.x - uv.x) : (uv2.y - uv.y);
	bool  isDir1   = distance1 < distance2;
	float distFinal = min(distance1, distance2);
	float edgeThick = distance1 + distance2;
	float pixelOff  = -distFinal / edgeThick + 0.5;

	bool correctVar = ((isDir1 ? lumaEnd1 : lumaEnd2) < 0.0) != (lumaM < lumaLocalAvg);
	float finalOff = correctVar ? pixelOff : 0.0;

	// Subpixel offset based on local luminance variance.
	float lumaAvg = (1.0/12.0) *
		(2.0*(lumaN + lumaS + lumaE + lumaW) + lumaNW + lumaNE + lumaSW + lumaSE);
	float subpixOff1 = clamp(abs(lumaAvg - lumaM) / range, 0.0, 1.0);
	float subpixOff2 = (-2.0*subpixOff1 + 3.0) * subpixOff1 * subpixOff1;
	float subpixOffFinal = subpixOff2 * subpixOff2 * subpix;
	finalOff = max(finalOff, subpixOffFinal);

	float2 finalUv = uv;
	if(horz) finalUv.y += finalOff * stepLength;
	else     finalUv.x += finalOff * stepLength;

	return float4(tex2D(tex0, finalUv).rgb, 1.0);
}
