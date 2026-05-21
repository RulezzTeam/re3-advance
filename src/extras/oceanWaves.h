#pragma once

#ifdef POSTFX_HDR

#include "common.h"

// Stage 21 — Ocean wave heightfield generator.
//
// Bakes an animated 256×256 RGBA16F texture each frame containing
//   .r  = sum-of-sinusoids displacement (height in metres)
//   .gb = surface derivative for normal reconstruction
//   .a  = foam mask (crests breaking)
//
// The water rendering pipeline (CWaterLevel and the existing water
// atomic shaders) can later sample this texture to displace + light
// the surface — that integration is Stage 21.2 and lives in the
// water vertex/pixel shaders. This stage lands the heightfield bake
// infrastructure so the data is available the moment a consumer
// wires it up.
//
// Cost: one 256² RGBA16F draw per frame. 4-octave summed sinusoids
// approximate a Phillips spectrum for moderate sea states; the
// future evolution is to swap the inner loop for an inverse-FFT
// lookup driven by a Phillips spectrum bake (Tessendorf 2001).

class COceanWaves
{
public:
	static RwRaster *pHeightField;	// 256² RGBA16F
	static bool Enabled;
	static float WindSpeed;		// m/s — drives wave energy
	static float WindDirX;		// unit vector
	static float WindDirY;
	static float Amplitude;		// metres — base wave height
	static float Choppiness;	// 0..2 — Gerstner-style crest sharpening
	static float FoamThreshold;	// 0..2 — slope above which crests foam
	static float TileSize;		// metres per 256² tile (world repeat)

	static void InitOnce(void);
	static void Open(RwCamera *cam);
	static void Close(void);
	// Per-frame bake. No-op when Enabled=false.
	static void Render(RwCamera *cam);
};

#endif
