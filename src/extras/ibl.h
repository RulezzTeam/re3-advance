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
		// Refresh every N frames. Faster = more reactive (lightning,
		// time-of-day) at the cost of more GPU. 30 is a good middle
		// ground: visible reaction within ~half a second, 6 cube face
		// renders per refresh = roughly free at half-second cadence.
		REFRESH_PERIOD = 30,
		// GGX prefilter mip chain depth — 6 levels covers roughness
		// 0, 0.2, 0.4, 0.6, 0.8, 1.0 which is the canonical step set
		// in the Karis split-sum literature. PREFILTER_SIZE = base
		// mip-0 size (256² gives clean sharp reflections; smaller
		// would visibly pixelate on car/glass surfaces).
		PREFILTER_SIZE = 256,
		PREFILTER_MIPS = 6,
	};

	static void *captureCube;	// IDirect3DCubeTexture9, RGBA16F, CaptureSize²
	static void *irradianceCube;	// same format, IrradianceSize²
	// GGX-prefiltered specular cube — 256² RGBA16F with PREFILTER_MIPS
	// mip levels. Mip 0 = roughness 0 (sharp), mip N-1 = roughness 1
	// (fully rough). Receiver samples via texCUBElod with mip =
	// roughness × (PREFILTER_MIPS-1). Replaces texCUBE(captureCube, R)
	// in the IBL specular path so rough surfaces actually look rough.
	// Refreshed each CIBL::Update alongside the captureCube refresh.
	static void *prefilterCube;
	// Split-sum BRDF LUT — 256×256 F16_RGBA 2D texture (.r = scale,
	// .g = bias for the Karis split-sum approximation). Baked once at
	// CIBL::Open from brdfLut_PS — no per-frame upkeep. Binds on PS
	// sampler s11 in default_pp_PS so the runtime IBL specular path
	// becomes prefiltered(R) * (F0 * LUT.r + LUT.g) for proper
	// Fresnel + roughness-aware reflectance.
	static void *brdfLut;
	static bool Enabled;		// menu toggle
	static int FrameCounter;	// drives the refresh schedule
	// Reflection strength — drives the Fresnel-weighted specular term
	// in default_pp_PS that samples captureCube on s8. 0 = off (just
	// diffuse IBL), 1 = neutral, 2 = vivid.
	static float ReflStrength;

	// Runtime-configurable cube sizes. Defaults bumped from 64/32 to
	// 128/64 — the previous defaults were Phase-1-ship-it picks; with
	// modern 4+ GB VRAM and the engine's <200 MB total, paying ~3 MB
	// for the larger captureCube is free. Menu sliders trigger a
	// Close+Open re-allocation via SizesAfterChange. Old values stay
	// valid as the "Small" tier entry in the menu list.
	//
	// The menu binds the int8 *Index fields; SizesAfterChange maps each
	// to the actual pixel size in {Capture,Irradiance}Size and calls
	// Reopen. Same shape as CustomPipes::EnvMapSizeAfterChange.
	static int32 CaptureSize;
	static int32 IrradianceSize;
	static int8  CaptureSizeIndex;       // 0..3 = 64/128/256/512
	static int8  IrradianceSizeIndex;    // 0..3 = 16/32/64/128

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

	// Re-open the cube allocations after the user changes CaptureSize /
	// IrradianceSize via menu. Idempotent + safe to call when CIBL is
	// disabled (no-op). Called from the menu CCFOSelect AfterChange hook.
	static void Reopen(void);

	// CCFOSelect AfterChange callbacks — map the index slot back to the
	// actual pixel size and trigger Reopen. Static + non-member-friendly
	// signature so CCFOSelect can take a plain function pointer.
	static void CaptureSizeAfterChange(int8 before, int8 after);
	static void IrradianceSizeAfterChange(int8 before, int8 after);

	// Bake the split-sum BRDF LUT into brdfLut. Runs once at Open after
	// the LUT raster is allocated. Idempotent — re-running just re-draws
	// the same content. Direct D3D9 quad render bypassing librw im2d for
	// the same reason captureCube uses direct dispatch (Open-time has no
	// live scene camera).
	static void BakeBrdfLut(void);
};

#endif
