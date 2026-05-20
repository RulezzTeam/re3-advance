// Temporal Anti-Aliasing pass.
//
// Inputs:
//   tex0 = current LDR frame (CPostFX::pBackBuffer)
//   tex1 = history buffer from previous frame (pTaaHistA/B ping-pong)
//   tex2 = G-buffer (RGB = world normal*0.5+0.5, A = linear depth)
// Output: temporally accumulated colour, written to the next history slot
//
// Reprojection — reconstruct per-pixel world position from gbuf depth +
// the camera's frustum corners, then project through the previous frame's
// view-projection matrix to find the matching UV in the history buffer.
// Camera motion is now properly handled; per-object motion is still
// missing (vehicles + peds will ghost a little on direction changes),
// which we mitigate via the YCoCg neighbourhood clamp.
//
// taaParams:
//   .x = blend factor (~0.10..0.15; smaller = sharper but more flicker)
//   .y = clamp aggressiveness (~1.0; smaller = tighter, less ghost)
//   .z = jitter X (current frame sub-pixel offset, used to compensate)
//   .w = jitter Y

sampler2D currentTex : register(s0);
sampler2D historyTex : register(s1);
sampler2D gbufTex    : register(s2);

float4 taaParams : register(c10);
float4 taaTexel  : register(c11);	// .xy = 1/W, 1/H

// Reprojection constants — same layout as the volumetric fog / SSR.
// .xyz = camera world pos, .w = farClip.
float4 taaCamera : register(c12);
// 4 frustum corner rays (uv = TL/TR/BR/BL) for view-ray reconstruction.
float4 taaRayTL  : register(c13);
float4 taaRayTR  : register(c14);
float4 taaRayBR  : register(c15);
float4 taaRayBL  : register(c16);
// Previous frame's world-to-clip matrix (row-major).
float4x4 taaPrevViewProj : register(c17);

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

// NaN-safe normalize — see hdrResolve_PS.hlsl. A NaN motion vector at a
// single pixel grows into a smeared cloud as the next frame's TAA reads it
// back, so guarding every reproject step matters even when input looks OK.
float3 SafeNormalize(float3 v)
{
	float l2 = dot(v, v);
	return v * rsqrt(max(l2, 1e-8));
}

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

	// Reproject through last frame's viewProj. Reconstruct world position
	// from gbuf depth + bilerped view ray, then transform to NDC under
	// prevViewProj. Off-screen reads fall back to current-frame UV.
	float4 gbuf = tex2D(gbufTex, uv);
	float  viewZ = gbuf.a * taaCamera.w;
	float3 ray = lerp(lerp(taaRayTL.xyz, taaRayTR.xyz, uv.x),
	                  lerp(taaRayBL.xyz, taaRayBR.xyz, uv.x),
	                  uv.y);
	float3 wp = taaCamera.xyz + ray * viewZ;
	float4 prevClip = mul(float4(wp, 1.0), taaPrevViewProj);
	float2 prevUV = uv;
	float onScreen = 1.0;
	if(abs(prevClip.w) > 1e-4){
		prevClip.xyz /= prevClip.w;
		prevUV = prevClip.xy * 0.5 + 0.5;
		prevUV.y = 1.0 - prevUV.y;
		// Reject off-screen reprojections (camera cut / sky / extreme
		// pan); the YCoCg clamp + same-UV fallback handles those.
		float2 chk = step(0.0, prevUV) * step(prevUV, 1.0);
		onScreen = chk.x * chk.y;
		if(gbuf.a < 0.0005) onScreen = 0.0;	// sky: no reproject
	}
	prevUV = lerp(uv, prevUV, onScreen);

	// Final NaN/Inf guard on the reprojected UV — if anything went wrong
	// in the reconstruction (degenerate ray, divide-by-zero in clip /= w
	// after the >1e-4 check), fall back to the current frame UV so the
	// shader doesn't bake garbage into the history.
	if(any(isnan(prevUV)) || any(isinf(prevUV))){
		prevUV = uv;
		onScreen = 0.0;
	}

	// History sample at the reprojected UV.
	float3 hisRgb = tex2D(historyTex, prevUV).rgb;
	float3 hisY   = RGB2YCoCg(hisRgb);

	// Clamp history into neighbourhood AABB — kills ghosting silhouettes
	// from moving objects (we have no per-object velocity yet) and
	// occlusion mismatches at frame boundaries.
	hisY = clamp(hisY, mn, mx);

	// Blend. When we couldn't reproject (off-screen / sky / new-pixel),
	// bias toward the current frame so we don't smear stale content.
	float blend = lerp(1.0, saturate(taaParams.x), onScreen);
	float3 outY = lerp(hisY, curY, blend);
	return float4(YCoCg2RGB(outY), 1.0);
}
