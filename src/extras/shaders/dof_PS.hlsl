// Depth of Field — hexagonal-kernel bokeh approximation.
//
// Two-step kernel (cheap):
//   1. Read the centre HDR colour + per-pixel CoC (circle of confusion)
//      derived from gbuf depth vs focal distance.
//   2. Sample N additional taps along 6 polygon vertices at radius CoC.
//      Each tap is weighted by its own CoC so background bokeh doesn't
//      bleed onto foreground silhouettes.
//
// Sampler bindings:
//   s0 = pHdrScene  — HDR colour
//   s1 = pGbufNormalDepth — A = linear viewZ/far (used to compute CoC)
//
// Constants:
//   c10: .x = farClip
//        .y = focusDistance (world units, distance the camera is sharp on)
//        .z = focusRange     (world units, ±half-window where everything stays sharp)
//        .w = aperture       (max CoC radius in UV units)
//   c11: 1/width, 1/height, 0, 0

sampler2D hdrTex  : register(s0);
sampler2D gbufTex : register(s1);

float4 dofParams : register(c10);
float4 dofTexel  : register(c11);

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

float ComputeCoC(float viewZ)
{
	// Linear CoC: 0 inside the focus window, ramps to aperture at the
	// edge of the field. Symmetric near/far blur.
	float dz = abs(viewZ - dofParams.y);
	float t = saturate((dz - dofParams.z) / max(dofParams.z, 0.5));
	return t * dofParams.w;
}

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float farClip = dofParams.x;

	float4 gbuf = tex2D(gbufTex, uv);
	float  centerZ = gbuf.a * farClip;
	float  centerCoC = ComputeCoC(centerZ);

	float3 centerCol = tex2D(hdrTex, uv).rgb;

	// Sample 12 hex-kernel taps: 6 inner ring + 6 outer ring (rotated 30°).
	// Each tap's contribution is weighted by its own CoC.
	const float2 kHex[12] = {
		float2( 1.000,  0.000), float2( 0.500,  0.866), float2(-0.500,  0.866),
		float2(-1.000,  0.000), float2(-0.500, -0.866), float2( 0.500, -0.866),
		float2( 0.866,  0.500), float2( 0.000,  1.000), float2(-0.866,  0.500),
		float2(-0.866, -0.500), float2( 0.000, -1.000), float2( 0.866, -0.500),
	};

	float3 acc = centerCol;
	float  wAcc = 1.0;

	[unroll]
	for(int i = 0; i < 12; i++){
		float2 ofs = kHex[i] * centerCoC * dofTexel.xy * 32.0;
		float2 sUv = uv + ofs;
		float4 sG  = tex2D(gbufTex, sUv);
		float  sZ  = sG.a * farClip;
		float  sCoC = ComputeCoC(sZ);

		// Each tap weight = how "blurry" that pixel is. Foreground taps
		// onto a sharp neighbour with high weight; background taps onto
		// a less-sharp neighbour with lower weight — avoids bleed.
		float w = sCoC * 0.5 + 0.1;
		acc += tex2D(hdrTex, sUv).rgb * w;
		wAcc += w;
	}

	return float4(acc / wAcc, 1.0);
}
