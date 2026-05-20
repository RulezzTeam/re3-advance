// Sky-to-cube-face capture pass.
//
// Writes a HDR sky + sun-disk colour into a cube face. The host runs
// this once per face (6 invocations) to populate the IBL capture cube.
// The resulting cube is then convolved by iblConvolve_PS to produce the
// diffuse irradiance cube sampled in default_pp_PS.
//
// The sky model here is a Hosek-Wilkie-inspired analytic function:
// Rayleigh-style cosine-of-zenith gradient for the base colour, plus
// a Mie-style forward-scattered glow around the sun. It's not the
// full HW coefficient table (which needs ~9 datasets per turbidity
// per channel); it's a stripped-down approximation that produces the
// same visual signature — bright sun halo, brighter horizon than
// pure 1-cos(theta), cooler zenith.
//
// CTimeCycle still drives the colour palette via the host — skyTop/
// skyHorizon/skyGround are sampled directly so dawn/dusk/night
// transitions stay grounded in the game's existing time-cycle data
// instead of going analytic.

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

	// HW-inspired analytic sky.
	//
	// theta = angle from zenith → drives the vertical gradient.
	// gamma = angle to the sun → drives Mie scattering halo.
	// Rayleigh-ish term: (1 + cos²(theta)) ≈ brighter near horizon.
	float cosTheta = saturate(D.z);
	float rayleigh = 0.5 * (1.0 + cosTheta * cosTheta);

	// Mie phase — Henyey-Greenstein with g ≈ 0.85 gives a tight, bright
	// forward-scattered halo around the sun.
	float cosGamma = saturate(dot(D, sunDir.xyz));
	float g  = 0.85;
	float g2 = g * g;
	float denom = 1.0 + g2 - 2.0 * g * cosGamma;
	float mie = (1.0 - g2) / (4.0 * 3.14159 * denom * sqrt(denom));

	// Base gradient — mix the time-cycle sky-top (zenith) and horizon
	// using the HW-style two-term split. cosTheta = 1 → all sky-top;
	// cosTheta = 0 → all horizon.
	float horW = 1.0 - cosTheta;
	float3 baseSky = skyTop.rgb * cosTheta + skyHorizon.rgb * horW;

	// Brightness modulation by Rayleigh — emphasises sky-top vs horizon.
	baseSky *= 0.6 + 0.4 * rayleigh;

	// Below the horizon → ground colour fade. Smooth transition centred
	// on D.z = 0 with ~0.05 radians width.
	float aboveHoriz = smoothstep(-0.05, 0.05, D.z);
	float3 skyCol = lerp(skyGround.rgb, baseSky, aboveHoriz);

	// Mie sun scattering — adds a bright glow that's strongest at the
	// sun direction and falls off smoothly. Only applied above the
	// horizon (and only when the sun itself is above; sunDir.z > 0).
	float sunAbove = saturate(sunDir.z * 4.0);
	skyCol += sunColor.rgb * mie * aboveHoriz * sunAbove;

	// Sun disk — sharp, on top of the Mie halo, for the bright spot.
	float sunMask = smoothstep(sunColor.a, sunColor.a + 0.0015, cosGamma);
	skyCol += sunColor.rgb * sunMask * 4.0 * aboveHoriz * sunAbove;

	return float4(skyCol, 1.0);
}
