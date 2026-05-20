#pragma once

#ifdef POSTFX_WATER_REFLECTION

// Planar water reflection — mirrors the scene about a horizontal water
// plane into a dedicated RGBA16F render target (CWaterReflection::pRT).
// Eventually the water shader will sample this RT with a normal-map
// distortion offset; for now this module just owns the reflection pass.
//
// The reflection camera shares the main scene's frame matrix but with a
// post-multiplied flip about waterPlaneZ. We render a stripped-down scene
// (no postfx, no env-map updates, no particles) using a recursion guard
// that the various pipeline callbacks check via CWaterReflection::bRendering.

class CWaterReflection
{
public:
	static RwRaster *pRT;		// RGBA16F reflection target
	static RwRaster *pZBuffer;	// non-MSAA depth shared with pRT
	static RwCamera *reflectionCam;
	static bool Enabled;
	static bool bRendering;
	static float WaterPlaneZ;	// world Z of the reflection plane
	static int32 Resolution;	// 256 / 512 / 1024 / 2048 — selectable via menu
	static int8  ResolutionIndex;	// menu binding for the size selector

	static void InitOnce(void);
	static void Open(RwCamera *cam);
	static void Close(void);
	static void RebuildResolution(void);
	static void Render(RwCamera *cam);

	// Menu CCFOSelect AfterChange — maps ResolutionIndex to Resolution
	// and rebuilds the RT/Z buffer. Calls into the existing
	// RebuildResolution() so the per-shader logic stays in one place.
	static void ResolutionAfterChange(int8 before, int8 after);
};

#endif
