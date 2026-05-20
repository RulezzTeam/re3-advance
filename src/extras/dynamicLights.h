#pragma once

#ifdef POSTFX_HDR

// Dynamic point lights — per-pixel array of nearby CPointLights uploaded
// once per scene render. Lets the per-pixel shader (default_pp_PS) apply
// car headlights / street lamps / muzzle flashes / explosions to ANY
// pp-rendered atomic, including the static buildings + props that the
// legacy re3 CEntity::SetupLighting() path completely skipped.
//
// The host walks CPointLights::aLights[] each frame, scores each light
// by (luminance × inverse-distance²) relative to the camera, takes the
// top N, and pushes them as PS uniform constants via librw helpers.
//
// 8 light slots — matches the librw lighting register layout and keeps
// the per-pixel cost bounded. Empty slots are zeroed so the shader's
// radius cutoff drops them cheaply.
class CDynamicLights
{
public:
	// 32 chosen as the upper bound — ps_3_0 has 224 float constants
	// total, and each light needs 2 vec4 (position+radius, colour+
	// intensity). 32 × 2 = 64 registers (c101..c164), leaving plenty of
	// headroom for CSM (c48..c62), spot shadow (c70..c75), and the IBL/
	// wetness/ssr constants. 64 lights would overflow the budget by 5
	// registers. In practice scenes rarely have more than ~20 active
	// CPointLights anyway (the engine itself caps at 128 + MAX_DIST=22
	// filter), so 32 is comfortable headroom.
	enum { MAX_LIGHTS = 32 };

	// Menu / settings.ini hookable.
	static bool Enabled;
	static float Intensity;	// global multiplier on light contribution
	static float Reach;	// soft cap on max light radius (1.0 = engine value)
	static int   MaxLights;	// 0..MAX_LIGHTS (capped to MAX_LIGHTS)

	static void InitOnce(void);
	static void Update(RwCamera *cam);
};

#endif
