#pragma once

#ifdef POSTFX_HDR

// Image-Based Lighting Phase 2 — capture the sky to a real D3D9 cubemap,
// convolve it into a diffuse-irradiance cube, then sample that cube
// inside default_pp_PS as the ambient term. Replaces the procedural
// hemisphere gradient when CIBL::Enabled is on; the gradient is kept
// as a fallback path for users / GPUs that can't afford the cubemap.

class CIBL
{
public:
	enum {
		// 64² source cube (sky capture). Small but enough — the destination
		// irradiance is only 32² and convolved from this.
		CAPTURE_SIZE   = 64,
		IRRADIANCE_SIZE = 32,
		// Refresh every N frames. Faster = more reactive (lightning,
		// time-of-day) at the cost of more GPU. 30 is a good middle
		// ground: visible reaction within ~half a second, 6 cube face
		// renders per refresh = roughly free at half-second cadence.
		REFRESH_PERIOD = 30,
	};

	static void *captureCube;	// IDirect3DCubeTexture9, RGBA16F, CAPTURE_SIZE²
	static void *irradianceCube;	// same format, IRRADIANCE_SIZE²
	static bool Enabled;		// menu toggle
	static int FrameCounter;	// drives the refresh schedule
	// Reflection strength — drives the Fresnel-weighted specular term
	// in default_pp_PS that samples captureCube on s8. 0 = off (just
	// diffuse IBL), 1 = neutral, 2 = vivid.
	static float ReflStrength;

	static void InitOnce(void);
	static void Open(RwCamera *cam);
	static void Close(void);

	// Once per frame (called from CGBuffer::BeginScenePass). If it's our
	// turn in the refresh cycle, captures one face of the source cube
	// from the world origin, then re-convolves the irradiance cube.
	static void Update(RwCamera *cam);

	// Bind the irradiance cube on PS sampler s7 + push a flag to librw
	// so default_pp_PS knows to sample it instead of the gradient.
	// Mirror call to drop the binding before postfx passes.
	static void BindReceiver(void);
	static void UnbindReceiver(void);
};

#endif
