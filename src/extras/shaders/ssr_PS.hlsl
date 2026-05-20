// Screen-Space Reflections.
//
// Reconstructs the per-pixel reflected ray from the G-buffer normal + view
// direction, marches it through screen-space against the depth raster, and
// samples the HDR scene colour at the hit point. Half-res; the compose
// step in hdrResolve_PS up-samples bilinearly.
//
// Inputs:
//   s0 = pGbufNormalDepth   (RGB = world normal*0.5+0.5, A = linear viewZ/far)
//   s1 = pHdrScene          (RGBA16F linear scene colour)
//
// Output: RGBA — RGB = reflected colour, A = confidence (0 = miss, 1 = hit).
//
// Constants:
//   c10: .xyz = camera world position, .w = farClip
//   c11..c14: 4 world-space frustum corner rays (TL, TR, BR, BL) — same
//             layout as the volumetric fog. Bilerped to get the per-pixel
//             view ray; world position at viewZ = camPos + ray * viewZ.
//   c15: 4x4 world-to-clip matrix (row-major; cols c15..c18). Used to
//        project intermediate march positions back to screen UVs.
//   c19: .x = max march distance (world units)
//        .y = step count (uniform fraction of max distance)
//        .z = depth-thickness (world units, accept hits within this Δz)
//        .w = global strength (0 = bypass)

sampler2D gbufTex : register(s0);
sampler2D hdrTex  : register(s1);

float4 ssrCamera  : register(c10);
float4 ssrRayTL   : register(c11);
float4 ssrRayTR   : register(c12);
float4 ssrRayBR   : register(c13);
float4 ssrRayBL   : register(c14);
float4x4 ssrViewProj : register(c15);
float4 ssrParams  : register(c19);

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

// Project a world-space point to NDC, then to UV [0,1] for tex2D.
float3 WorldToUVDepth(float3 wp)
{
	float4 clip = mul(float4(wp, 1.0), ssrViewProj);
	clip.xyz /= max(clip.w, 1e-5);
	float2 uv = clip.xy * 0.5 + 0.5;
	uv.y = 1.0 - uv.y;	// D3D9 UV flip
	return float3(uv, clip.z);
}

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float4 gbuf = tex2D(gbufTex, uv);

	// Sky / cleared pixels — no surface to reflect.
	if(gbuf.a < 0.0005 || dot(gbuf.rgb - 0.5, gbuf.rgb - 0.5) < 0.002)
		return float4(0, 0, 0, 0);

	if(ssrParams.w < 0.001)
		return float4(0, 0, 0, 0);

	float3 N = normalize(gbuf.rgb * 2.0 - 1.0);

	// Bilerp the world-space view ray (uv=0,0=TL).
	float3 ray = lerp(lerp(ssrRayTL.xyz, ssrRayTR.xyz, uv.x),
	                  lerp(ssrRayBL.xyz, ssrRayBR.xyz, uv.x),
	                  uv.y);
	float3 V = normalize(ray);

	// Reconstruct hit (current pixel) world position from gbuf.a × farClip.
	float viewZ = gbuf.a * ssrCamera.w;
	float3 wp = ssrCamera.xyz + ray * viewZ;

	// Reflected world-space ray (V points from camera to fragment, so
	// reflect across N).
	float3 R = reflect(V, N);

	// Skip rays pointing backward into the camera — common on grazing
	// silhouettes; they always escape the screen anyway.
	float backFace = -dot(R, V);
	if(backFace < 0.05)
		return float4(0, 0, 0, 0);

	// Linear screen-space march. Step length is constant in world units —
	// good enough for the half-res quality target. Step count from .y.
	float maxDist = ssrParams.x;
	int   stepN  = (int)max(ssrParams.y, 4.0);
	float stepLen = maxDist / (float)stepN;
	float thickness = ssrParams.z;

	// Per-pixel dither — kills the visible "ring" pattern when a flat
	// surface samples the march grid synchronously.
	float jitter = frac(sin(dot(uv * 11.13, float2(12.9898, 78.233))) * 43758.5453);

	float3 hitColor = float3(0, 0, 0);
	float  hitMask = 0.0;
	float  alive = 1.0;	// runtime "found" gate, replaces break for ps_3_0

	[loop]
	for(int i = 0; i < 32; i++){
		// Bail out of further work once we've found a hit (or gone off-
		// screen). We can't `break` here because the gbuf/hdr tex2D
		// calls below use computed UVs (gradient instructions can't live
		// in a loop with break in ps_3_0). Multiplying alive into the
		// hit accumulator and skipping additional writes is equivalent.
		float doStep = alive * step(float(i), float(stepN - 1));
		float t = (float(i) + jitter) * stepLen;
		float3 sp = wp + R * t;

		float3 spProj = WorldToUVDepth(sp);
		float2 spUV  = spProj.xy;

		float onScreen = step(0.0, spUV.x) * step(0.0, spUV.y)
		               * step(spUV.x, 1.0) * step(spUV.y, 1.0);

		// Sample unconditionally; we'll mask the result. Use tex2Dlod
		// with LOD 0 to avoid gradient calculation off-screen.
		float4 spGbuf = tex2Dlod(gbufTex, float4(spUV, 0, 0));
		float spViewZ = spGbuf.a * ssrCamera.w;
		float spRayZ = length(sp - ssrCamera.xyz);
		float dz = spRayZ - spViewZ;

		float spHasGeo = step(0.0005, spGbuf.a);
		float depthOK  = step(0.0, dz) * step(dz, thickness);
		float thisHit  = doStep * onScreen * spHasGeo * depthOK;

		float4 col = tex2Dlod(hdrTex, float4(spUV, 0, 0));
		float2 fade = smoothstep(0.0, 0.08, spUV) * smoothstep(0.0, 0.08, 1.0 - spUV);

		hitColor = lerp(hitColor, col.rgb, thisHit);
		hitMask  = lerp(hitMask,  fade.x * fade.y, thisHit);
		// Once we hit, mute further iterations.
		alive *= 1.0 - thisHit;
		// If we go off-screen, also stop further hits.
		alive *= onScreen;
	}

	return float4(hitColor, hitMask * ssrParams.w);
}
