// Sky-to-cube-face capture pass.
//
// Writes a HDR sky + sun-disk colour into a cube face. The host runs
// this once per face (6 invocations) to populate the IBL capture cube.
// The resulting cube is then convolved by iblConvolve_PS to produce the
// diffuse irradiance cube sampled in default_pp_PS.
//
// We don't capture the actual scene here — that's a Phase 3 upgrade.
// What we DO capture: the analytic sky gradient (sky-top / sky-bottom /
// ground tint, same source as the procedural Phase 1 IBL) plus a sun
// disk evaluated against the world sun direction. The convolution then
// turns that into a directional irradiance cube that the lighting
// shader can read in one tap.

// Face basis. The destination texel's world direction is reconstructed
// per pixel from face-space (-1..1) → R*x + U*y + F.
float4 faceForward : register(c10);
float4 faceRight   : register(c11);
float4 faceUp      : register(c12);

// Sky colours — same content as the procedural IBL gradient.
float4 skyTop      : register(c13);	// zenith colour
float4 skyHorizon  : register(c14);	// horizon ring
float4 skyGround   : register(c15);	// nadir / earth tone

// .xyz = world direction TOWARD the sun, .w = sun disk hardness
float4 sunDir      : register(c16);
// .rgb = sun emissive colour (HDR), .a = sun radius cosine
float4 sunColor    : register(c17);

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

float4 main(VS_out input) : COLOR
{
	// Reconstruct the world direction for this destination texel.
	float2 fc = input.TexCoord0 * 2.0 - 1.0;
	float3 D = normalize(faceForward.xyz + fc.x * faceRight.xyz + fc.y * faceUp.xyz);

	// Sky gradient — same hemisphere split as the IBL receiver.
	float up   = saturate( D.z);
	float down = saturate(-D.z);
	float horW = 1.0 - saturate(pow(abs(D.z), 2.2));
	float3 skyCol = up   * skyTop.rgb
	              + down * skyGround.rgb
	              + horW * skyHorizon.rgb;

	// Sun disk — soft-edged circle around the sun direction.
	float cosTheta = dot(D, sunDir.xyz);
	float sunMask  = smoothstep(sunColor.a, sunColor.a + 0.005, cosTheta);
	skyCol += sunColor.rgb * sunMask;

	// Optional broad halo around the sun — captures bloom-like falloff
	// without needing a separate pass.
	float halo = smoothstep(0.92, 1.0, cosTheta);
	skyCol += sunColor.rgb * halo * 0.15;

	return float4(skyCol, 1.0);
}
