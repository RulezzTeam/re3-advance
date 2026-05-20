// Depth of Field — hexagonal-bokeh single-pass blur.
//
// Single-pass alternative to the McGuire 3-pass streak approach. 19 taps
// arranged in a proper FILLED hexagonal disc — centre + 6 inner ring
// (radius 0.5) + 12 outer ring (radius 1.0, alternating cardinal +
// rotated cardinal). The previous version had all 12 taps at radius 1.0,
// which only sampled a thin RING and produced visibly "donut" bokeh on
// out-of-focus highlights. The new pattern integrates the disc properly.
//
//      .   .   .       outer ring (radius 1.0)
//        .   .         inner ring (radius 0.5)
//      .   o   .       centre
//        .   .
//      .   .   .
//
// Each tap weighted by its own CoC so background bokeh doesn't bleed
// onto foreground silhouettes (the classic Bokeh halo bug).
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

	// Filled hexagonal disc — 18 taps + centre = 19 total. Inner ring
	// (radius 0.5, 6 taps) catches near-centre bokeh; outer rings
	// (radius 1.0, 12 taps at every 30°) define the hex silhouette.
	// Doubled outer-ring resolution gives a clean hex EDGE rather than
	// a ring artefact at large aperture.
	const float2 kHex[18] = {
		// Inner ring — radius 0.5, 6 hex vertices.
		float2( 0.500,  0.000), float2( 0.250,  0.433), float2(-0.250,  0.433),
		float2(-0.500,  0.000), float2(-0.250, -0.433), float2( 0.250, -0.433),
		// Outer ring — radius 1.0, 12 taps every 30° for smooth hex edge.
		float2( 1.000,  0.000), float2( 0.866,  0.500), float2( 0.500,  0.866),
		float2( 0.000,  1.000), float2(-0.500,  0.866), float2(-0.866,  0.500),
		float2(-1.000,  0.000), float2(-0.866, -0.500), float2(-0.500, -0.866),
		float2( 0.000, -1.000), float2( 0.500, -0.866), float2( 0.866, -0.500),
	};

	float3 acc = centerCol;
	float  wAcc = 1.0;

	[unroll]
	for(int i = 0; i < 18; i++){
		float2 ofs = kHex[i] * centerCoC * dofTexel.xy * 32.0;
		float2 sUv = uv + ofs;
		float4 sG  = tex2D(gbufTex, sUv);
		float  sZ  = sG.a * farClip;
		float  sCoC = ComputeCoC(sZ);

		// Each tap weight = how "blurry" that pixel is. Foreground taps
		// onto a sharp neighbour with high weight; background taps onto
		// a less-sharp neighbour with lower weight — avoids bleed.
		// Inner-ring taps get a slightly stronger base weight (0.15 vs
		// 0.10) so the disc fills evenly rather than reading as ring +
		// rim.
		float baseW = (i < 6) ? 0.15 : 0.10;
		float w = sCoC * 0.5 + baseW;
		acc += tex2D(hdrTex, sUv).rgb * w;
		wAcc += w;
	}

	return float4(acc / wAcc, 1.0);
}
