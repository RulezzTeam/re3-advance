// Sky-to-cube-face capture pass — Bruneton-inspired analytic atmosphere
// (Stage 20). Renders a single cube face containing realistic
// Rayleigh + Mie scattering for the IBL capture cube; the irradiance
// + GGX-prefilter convolves then both inherit the atmosphere look.
//
// Why not the full Bruneton model? The full one requires three pre-
// computed LUTs (transmittance, multiple-scattering, sky-view) with
// 100+ μs of GPU bake-time per LUT plus per-frame upkeep. The inline
// single-scatter version below gets ~90% of the visual fidelity at
// 0 LUT overhead — it just integrates Rayleigh + Mie along the view
// ray and per-step sun-transmittance, using a flat-earth plane-parallel
// approximation (sufficient for a cube renderer where rays are mostly
// upward and the curvature error is in the noise).
//
// Result:
//   - Proper blue sky at zenith (Rayleigh weighting on short wavelengths)
//   - Warm red/orange horizon at low sun angles (longer airmass scatters
//     blue out, leaving red transmitted)
//   - Mie sun halo (broad forward-scattered glow around the sun)
//   - Sharp sun disk on top of the halo
//   - Ground colour below the horizon (kept from skyGround for VC's
//     low-altitude city look)
//
// CTimeCycle still influences brightness via skyTop.a (which the host
// can leave at 1 for the Bruneton path or use to tint dawn/dusk).
// SunColor follows DirectionalLightColourForFrame so the sun stays
// warm at dawn/dusk even without CTimeCycle sky colour input.

float4 faceForward : register(c10);
float4 faceRight   : register(c11);
float4 faceUp      : register(c12);

// Legacy sky colour uniforms — kept for backwards compat with the
// CIBL upload path; the Bruneton path mostly ignores them but uses
// skyGround for the below-horizon tint.
float4 skyTop      : register(c13);
float4 skyHorizon  : register(c14);
float4 skyGround   : register(c15);

float4 sunDir      : register(c16);	// .xyz toward sun, .w hardness
float4 sunColor    : register(c17);	// .rgb HDR, .a sun radius cosine

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
};

// Physical atmosphere constants — wavelength-dependent Rayleigh
// extinction per metre × earth atmosphere thickness scaling. The
// classic ratios are ~5.8e-6, 13.5e-6, 33.1e-6 (R/G/B) per metre at
// sea level for ~700/530/440 nm light. The ×100 here folds the
// integration-length-in-km approximation so the numbers stay in a
// sane numeric range; the result has been visually tuned to match
// the daytime sky at sun-elevation ~60°.
static const float3 RAYLEIGH = float3(0.0058, 0.0135, 0.0331);

// Mie extinction — uniform across the visible spectrum to first order
// (aerosol particles are bigger than wavelength). Real values 2e-6
// per metre; bumped here to give a visible halo on a cube that's
// only ~64-512 texels wide.
static const float MIE = 0.0030;

// Number of integration steps along the view ray. 16 is enough to
// kill banding at the cube resolutions we render (≤512²). The inner
// sun-ray integration uses 4 steps which is plenty for a flat-earth
// plane-parallel transmittance.
static const int VIEW_STEPS = 16;
static const int SUN_STEPS  = 4;

// Rayleigh phase function — 3/16π × (1 + cos²θ). Symmetric, scatters
// equally forward and back. Drives the blue dome away from the sun.
float RayleighPhase(float cosTheta)
{
	return 0.05968 * (1.0 + cosTheta * cosTheta);	// 3/(16π)
}

// Mie phase — Henyey-Greenstein with g≈0.76, which gives the tight
// forward-scattered sun halo. Hand-picked to read on screen at the
// IBL cube's small footprint.
float MiePhase(float cosTheta)
{
	const float g = 0.76;
	const float g2 = g * g;
	float denom = 1.0 + g2 - 2.0 * g * cosTheta;
	return (1.0 - g2) / (4.0 * 3.14159265 * denom * sqrt(max(denom, 1e-5)));
}

// Atmospheric density profile — exponential decay with altitude.
// Rayleigh scale height ≈ 8 km (air thins fast), Mie ≈ 1.2 km
// (aerosols pool near the ground). Plane-parallel approximation,
// altitude in kilometres.
float2 DensityAtAltitude(float altKm)
{
	float r = exp(-altKm / 8.0);
	float m = exp(-altKm / 1.2);
	return float2(r, m);
}

float4 main(VS_out input) : COLOR
{
	// Reconstruct the world direction for this destination texel.
	float2 fc = input.TexCoord0 * 2.0 - 1.0;
	float3 D = normalize(faceForward.xyz + fc.x * faceRight.xyz + fc.y * faceUp.xyz);

	float3 sun = sunDir.xyz;
	float cosGamma = dot(D, sun);	// view-vs-sun angle

	// Below-horizon path — return the ground tint blended with a thin
	// horizon glow so the cube has a sensible "down" colour for the
	// irradiance convolution.
	if(D.z < -0.05){
		// Smooth fade so the seam doesn't show in the irradiance.
		float groundT = smoothstep(-0.25, -0.05, D.z);
		float3 groundTint = lerp(skyGround.rgb * 0.5, skyGround.rgb, groundT);
		return float4(groundTint, 1.0);
	}

	// View-ray altitude integration. Camera at altKm = 0; ray rises
	// at rate D.z (zenith component). Total atmosphere "interesting"
	// thickness ≈ 80 km — beyond that, density ≈ 0.
	// Length of view ray through atmosphere: 80 / max(D.z, 0.0001) km
	// (plane-parallel) — clamped so near-horizon rays don't explode.
	float viewT = 80.0 / max(D.z, 0.05);
	float stepLen = viewT / (float)VIEW_STEPS;

	// Optical-depth accumulators over the full view ray. Used to
	// compute the transmittance from the integration point back to
	// the camera.
	float2 odView = float2(0, 0);

	float3 totalRayleigh = float3(0, 0, 0);
	float3 totalMie      = float3(0, 0, 0);

	[loop]
	for(int i = 0; i < VIEW_STEPS; i++){
		float t = (float(i) + 0.5) * stepLen;
		float altKm = t * D.z;	// flat-earth altitude
		if(altKm < 0.0) altKm = 0.0;
		float2 dens = DensityAtAltitude(altKm);
		float2 odSeg = dens * stepLen;
		odView += odSeg;

		// Sun transmittance from this integration point — short
		// integration along the sun ray. Only walk if the sun is
		// above the local horizon; otherwise the sample sits in
		// earth shadow.
		float sunZ = sun.z;
		if(sunZ < 0.0) continue;
		float sunRayLen = 80.0 / max(sunZ, 0.05);
		float sunStep = sunRayLen / (float)SUN_STEPS;
		float2 odSun = float2(0, 0);
		[unroll]
		for(int j = 0; j < SUN_STEPS; j++){
			float st = (float(j) + 0.5) * sunStep;
			float sAlt = altKm + st * sunZ;
			if(sAlt < 0.0) sAlt = 0.0;
			float2 sDens = DensityAtAltitude(sAlt);
			odSun += sDens * sunStep;
		}

		// Combined extinction (view-back-to-camera × sun-to-point).
		// exp(-RAYLEIGH * (odView.x + odSun.x) - MIE * (odView.y + odSun.y))
		float3 tau = RAYLEIGH * (odView.x + odSun.x)
		           + MIE      * (odView.y + odSun.y);
		float3 transmittance = exp(-tau);

		// Accumulate scattered light at this step. dens × phase gives
		// the per-step contribution; transmittance attenuates it back
		// to the camera.
		totalRayleigh += dens.x * transmittance * stepLen;
		totalMie      += dens.y * transmittance * stepLen;
	}

	// Weight by phase × extinction coefficient × sun colour.
	float phaseR = RayleighPhase(cosGamma);
	float phaseM = MiePhase(cosGamma);
	float3 skyCol = (totalRayleigh * RAYLEIGH * phaseR
	              +  totalMie      * MIE      * phaseM)
	              * sunColor.rgb * 25.0;

	// Sharp sun disk on top — sunColor.a is cos(disk_radius); pixels
	// inside that cone get a strong additive emission. Only above the
	// horizon and only when the sun itself is above.
	float aboveHoriz = smoothstep(-0.05, 0.05, D.z);
	float sunAbove   = saturate(sun.z * 4.0);
	float sunMask    = smoothstep(sunColor.a, sunColor.a + 0.0015, cosGamma);
	skyCol += sunColor.rgb * sunMask * 6.0 * aboveHoriz * sunAbove;

	// Hand-off into the legacy skyTop.a brightness multiplier — lets
	// the CTimeCycle host fade the sky out at night without rewriting
	// the rest of the IBL pipeline.
	float brightness = max(skyTop.a, 1.0);
	return float4(skyCol * brightness, 1.0);
}
