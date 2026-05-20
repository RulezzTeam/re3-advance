#pragma once

#ifdef POSTFX_CSM

// Cascaded Shadow Maps — sun-aligned orthographic shadow maps that cover
// the view frustum across multiple distance bands ("cascades"). This is
// the infrastructure side; the receiver lives inside default_PS once the
// SHADOWS_CSM variant is wired up.
//
// Cost: 3 cascades x 2048x2048 R32F = ~50 MB VRAM. Render time ~scene
// depth-pass-per-cascade; we lean on frustum culling per cascade to keep
// it under control.

#define CSM_NUM_CASCADES 3
#define CSM_DEFAULT_SIZE 2048

class CCSM
{
public:
	struct Cascade {
		RwRaster *depthRT;	// R32F linear depth
		RwRaster *zBuffer;	// shared per-cascade (recreated on size change)
		RwCamera *lightCam;	// sun-aligned ortho camera
		float lightViewProj[16];	// row-major 4x4 (consumer-friendly)
		float splitDist;	// view-space depth boundary
	};

	static Cascade Cascades[CSM_NUM_CASCADES];
	static bool Enabled;
	static bool bRendering;		// guard so pipelines can early-out
	static int32 MapSize;		// per-cascade resolution (1024 / 2048 / 4096)
	static float Strength;		// 0..1 mix into the lit term
	static float Bias;		// depth comparison bias (~0.001..0.01)
	static int32 NumCascades;	// usable cascades (1..CSM_NUM_CASCADES)
	static int32 SoftnessMode;	// 0 = 4-tap PCF, 1 = 16-tap soft PCF
	static float SoftnessRadius;	// radius multiplier on the PCF texel step

	static void InitOnce(void);
	static void Open(RwCamera *cam);
	static void Close(void);
	static void ComputeCascades(RwCamera *cam);
	static void RenderShadowMaps(RwCamera *cam);

	// Bind the 3 cascade depth maps on samplers s4..s6 + upload the
	// matrix block + tuning constants. Called by CGBuffer right after
	// BeginScenePass so the receiver in default_pp_PS already sees the
	// cascades during the opaque world pass.
	static void BindReceiver(void);
	static void UnbindReceiver(void);
};

extern void *csmDepthVS;
extern void *csmDepthPS;

#endif
