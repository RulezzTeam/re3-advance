#pragma once

#ifdef POSTFX_HDR

#include "common.h"
#include "math/Vector.h"

// Stage 15 — Deferred decals.
//
// Lightweight per-pixel decal system that splats bullet holes, blood,
// paint marks etc. directly onto pHdrScene using the G-buffer depth
// to clip the splat to whatever surface lies under the decal's world
// position. Runs as a single additive full-screen pass between SSGI
// and ResolveHDR, so decals correctly receive tonemap + bloom + DoF
// downstream — no double-overlay artefacts.
//
// Capacity: 64 decals stored host-side; up to 16 per frame are uploaded
// to the shader (scored by recency + distance to camera so the visible
// ones win in dense fire-fights). Each decal contributes (pos, radius,
// rgb, alpha) — 32 floats packed into 8 vec4 registers per decal pair.
//
// Lifecycle:
//   InitOnce()    — engine startup, zero state
//   Add()         — game code calls this when a bullet / blood / paint
//                   event occurs. Decal slots wrap-around (oldest
//                   replaced first) at MAX_DECALS so we never grow.
//   Update()      — ages out expired decals each frame (cheap)
//   Render(cam)   — emits the splat pass (no-op when Enable=false)

class CDecals
{
public:
	enum Type {
		BULLET_HOLE = 0,	// small dark splat (0.2m radius)
		BLOOD       = 1,	// deep red, medium (0.6m radius)
		PAINT       = 2,	// vivid colour, larger (0.8m radius)
		OIL         = 3,	// dark slick, large (1.2m radius)
	};

	struct Decal {
		CVector pos;
		float radius;
		float r, g, b;
		float ageMs;	// time since spawn; > MaxAgeSeconds → cleared
		uint8 type;
		uint8 inUse;	// 0 = empty slot, 1 = active
		uint8 pad[2];
	};

	enum { MAX_DECALS = 64, MAX_PER_FRAME = 16 };

	static Decal decals[MAX_DECALS];
	static int writeCursor;	// next slot to overwrite (wraps)
	static bool Enabled;
	static float MaxAgeSeconds;	// 30s default — fades older decals

	static void InitOnce(void);
	// Spawn a new decal at world `pos` with surface `normal` (unit).
	// Type drives the default radius / colour palette; the caller can
	// override radius if they want a specific size. Safe to call from
	// any thread that holds the render lock.
	static void Add(CVector pos, CVector normal, Type type, float radiusOverride = 0.0f);
	static void Update(void);
	static void Render(RwCamera *cam);
};

#endif
