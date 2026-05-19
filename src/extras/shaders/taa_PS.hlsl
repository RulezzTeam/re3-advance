// Temporal Anti-Aliasing pass.
//
// Inputs:
//   tex0 = current LDR frame (CPostFX::pBackBuffer)
//   tex1 = history buffer from previous frame (pTaaHistA/B ping-pong)
// Output: temporally accumulated colour, written to the next history slot
//
// Reprojection is currently camera-only — we sample the history at the
// same UV (small camera motion gives acceptable results) but clamp the
// history into the 3x3 neighbourhood AABB to suppress ghosting on object
// motion. Per-object motion vectors are a future upgrade.
//
// taaParams:
//   .x = blend factor (~0.10..0.15; smaller = sharper but more flicker)
//   .y = clamp aggressiveness (~1.0; smaller = tighter, less ghost)
//   .z = jitter X (current frame sub-pixel offset, used to compensate)
//   .w = jitter Y

sampler2D currentTex : register(s0);
sampler2D historyTex : register(s1);

float4 taaParams : register(c10);
float4 taaTexel  : register(c11);	// .xy = 1/W, 1/H

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

// YCoCg encode/decode — gives a perceptually meaningful clamp space.
float3 RGB2YCoCg(float3 c){
	return float3(
		 0.25*c.r + 0.5*c.g + 0.25*c.b,
		 0.5 *c.r           - 0.5 *c.b,
		-0.25*c.r + 0.5*c.g - 0.25*c.b);
}

float3 YCoCg2RGB(float3 c){
	return float3(c.x + c.y - c.z,
	              c.x        + c.z,
	              c.x - c.y - c.z);
}

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float2 te = taaTexel.xy;

	// Current frame sample, with optional sub-pixel jitter compensation —
	// resample at uv - jitter so the camera-jitter-induced offset cancels.
	float3 curRgb = tex2D(currentTex, uv - taaParams.zw * te).rgb;
	float3 curY   = RGB2YCoCg(curRgb);

	// 3x3 neighbourhood AABB in YCoCg space for variance-based clamping.
	float3 mn = curY;
	float3 mx = curY;
	float3 mean = curY;
	float  count = 1.0;
	[unroll(2)]
	for(int dy = -1; dy <= 1; dy += 2){
		[unroll(2)]
		for(int dx = -1; dx <= 1; dx += 2){
			float3 c = RGB2YCoCg(tex2D(currentTex, uv + float2(dx, dy) * te).rgb);
			mn = min(mn, c);
			mx = max(mx, c);
			mean += c;
			count += 1.0;
		}
	}
	mean /= count;
	float3 ext = (mx - mn) * taaParams.y;
	mn = mean - ext * 0.5;
	mx = mean + ext * 0.5;

	// History sample at the same UV (camera-only reproject).
	float3 hisRgb = tex2D(historyTex, uv).rgb;
	float3 hisY   = RGB2YCoCg(hisRgb);

	// Clamp history into neighbourhood AABB — kills ghosting silhouettes
	// when objects move and we don't have per-pixel velocity.
	hisY = clamp(hisY, mn, mx);

	// Blend.
	float3 outY = lerp(hisY, curY, saturate(taaParams.x));
	return float4(YCoCg2RGB(outY), 1.0);
}
