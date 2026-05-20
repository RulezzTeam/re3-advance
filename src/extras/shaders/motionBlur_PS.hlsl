// Motion-vector-driven motion blur.
//
// Replaces the legacy "alpha-blend the previous frame on top" approach
// (CMBlur::OverlayRender on pFrontBuffer), which has two well-known
// failure modes the user hit:
//   1. At certain camera angles the previous frame's content has
//      virtually nothing in common with the current frame (camera cut,
//      sharp pan), so the alpha overlay reads as a doubled / smeared
//      ghost of the prior view.
//   2. The legacy path uses pow2-sized pFrontBuffer which UV-stretches
//      the previous frame across the camera-sized backbuffer, mismatched
//      by ~5% at 1920×1080 in a 2048² front buffer.
//
// This shader reconstructs the per-pixel motion vector from the gbuf
// depth + the previous-frame view-projection matrix (same approach the
// TAA reproject path uses), then blurs the current frame along that
// vector. Result: motion blur that follows scene + camera motion
// correctly, with no temporal ghosting on view cuts.
//
// Sampler bindings:
//   s0 = current LDR scene (CPostFX::pBackBuffer)
//   s1 = pGbufNormalDepth (RGB = world-normal*0.5+0.5, A = linear depth)
//
// Constants:
//   c10: blur params (.x = intensity 0..1, .y = max blur in screen UV
//        units, .z = sample count, .w = unused)
//   c11: camera world position (.xyz) + farClip (.w)
//   c12..c15: world-space frustum corner rays (TL/TR/BR/BL) — same
//             layout as the volumetric fog / SSR / TAA passes.
//   c16..c19: previous-frame world-to-clip 4×4 matrix.

sampler2D currentTex : register(s0);
sampler2D gbufTex    : register(s1);

float4 mbParams  : register(c10);
float4 mbCamera  : register(c11);
float4 mbRayTL   : register(c12);
float4 mbRayTR   : register(c13);
float4 mbRayBR   : register(c14);
float4 mbRayBL   : register(c15);
float4x4 mbPrevViewProj : register(c16);

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float3 curCol = tex2D(currentTex, uv).rgb;

	// If MV blur is disabled (intensity ~0) skip the per-pixel work.
	if(mbParams.x < 0.001)
		return float4(curCol, 1.0);

	// Reconstruct world pos at this pixel from gbuf depth + view ray.
	float4 gbuf = tex2D(gbufTex, uv);
	// Sky / cleared pixels (alpha 0) have no usable depth — leave the
	// current colour alone so the sky doesn't get smeared by a junk MV.
	if(gbuf.a < 0.0005)
		return float4(curCol, 1.0);

	float viewZ = gbuf.a * mbCamera.w;
	float3 ray = lerp(lerp(mbRayTL.xyz, mbRayTR.xyz, uv.x),
	                  lerp(mbRayBL.xyz, mbRayBR.xyz, uv.x),
	                  uv.y);
	float3 wp = mbCamera.xyz + ray * viewZ;

	// Project world position through last frame's view-proj matrix to
	// find where this pixel WAS in the previous frame.
	float4 prevClip = mul(float4(wp, 1.0), mbPrevViewProj);
	if(abs(prevClip.w) < 1e-4)
		return float4(curCol, 1.0);
	prevClip.xyz /= prevClip.w;
	float2 prevUV = prevClip.xy * 0.5 + 0.5;
	prevUV.y = 1.0 - prevUV.y;

	// Reject NaN / Inf prev UV — same defensive pattern as TAA.
	if(any(isnan(prevUV)) || any(isinf(prevUV)))
		return float4(curCol, 1.0);

	// Motion vector = current uv - prev uv (in normalised screen space).
	float2 mv = uv - prevUV;
	// Cap the per-pixel blur magnitude so a far-clip pixel with a
	// massive reprojection delta doesn't smear into space.
	float maxBlur = mbParams.y;
	float mvLen = length(mv);
	if(mvLen > maxBlur)
		mv *= maxBlur / mvLen;
	// Scale by user-set intensity dial.
	mv *= mbParams.x;

	// 8 taps along the motion vector, weighted by a triangle window so
	// the centre tap dominates (kills the "double image" you'd get with
	// uniform weights).
	const int   SAMPLES = 8;
	const float invSamples = 1.0 / float(SAMPLES);
	float3 acc = curCol;
	float  w   = 1.0;	// centre weight
	[unroll]
	for(int i = 1; i < SAMPLES; i++){
		float t = float(i) * invSamples;
		// Sample on BOTH sides of the centre for a symmetric blur kernel.
		// Triangle-window weight: w_i = 1 - t.
		float wi = 1.0 - t;
		float2 ofs = mv * (t - 0.5);	// -0.5..+0.5 centred
		float2 sUv = uv + ofs;
		// Clamp to screen so the bilinear sampler doesn't pull from
		// off-edge garbage; mirror would also work but clamp is cheaper.
		sUv = saturate(sUv);
		acc += tex2D(currentTex, sUv).rgb * wi;
		w += wi;
	}

	return float4(acc / w, 1.0);
}
