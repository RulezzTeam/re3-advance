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
	enum { MAX_LIGHTS = 8 };

	// Menu / settings.ini hookable.
	static bool Enabled;
	static float Intensity;	// global multiplier on light contribution
	static float Reach;	// soft cap on max light radius (1.0 = engine value)
	static int   MaxLights;	// 0..MAX_LIGHTS (capped to MAX_LIGHTS)

	static void InitOnce(void);
	static void Update(RwCamera *cam);
};

#endif
