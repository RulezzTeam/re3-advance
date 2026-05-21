#pragma once

#ifdef POSTFX_HDR

#include "common.h"
#include "math/Vector.h"

// Stage 34 — Probe Manager.
//
// Foundation for the probe-based lighting infrastructure (Stages 35-39).
// At level-load time we walk the CWorld building pool, pick "interesting"
// locations (building centres + sparse grid-fill in open space), and
// register a CProbe entry for each. Downstream subsystems then attach
// their own per-probe payload (cubemap RT for reflection probes, 27-
// float SH coefficients for SH light probes, single-scalar AO for
// occlusion probes, etc.).
//
// The base manager itself is payload-agnostic — it just owns the
// list of (position, type, radius, dirty-flag) tuples and a fast
// nearest-N lookup. Each subsystem walks the list during its own
// Open()/Bake()/Update() to attach payload.
//
// Memory: 256 probes × ~48 bytes per CProbe = ~12 KB at the manager
// level. Per-probe payload sizes vary by type (see Stage 35+ headers).
//
// Lifecycle:
//   InitOnce()     — once at engine startup, zero state
//   Bootstrap(cam) — after CWorld+streaming load, scan buildings,
//                    populate the probe list
//   FindNearestN() — per-scene-pass on the host side; result feeds
//                    a small constant block uploaded to shaders that
//                    need per-pixel probe lookups (Stage 36 SH probes)
//   Clear()        — destroy registry (level change, save load)

class CProbeManager
{
public:
	enum ProbeType {
		PROBE_REFLECTION    = 0,	// per-location prefiltered cubemap
		PROBE_SH            = 1,	// 9 SH coefficients × 3 channels
		PROBE_OCCLUSION     = 2,	// single AO scalar
		PROBE_BENT_NORMAL   = 3,	// CVector unoccluded direction
		PROBE_ATMOSPHERE    = 4,	// 3 vec3 (sky/horizon/ground) tint
		PROBE_TYPE_COUNT,
	};

	struct CProbe {
		CVector pos;
		float radius;		// influence sphere — 0 = point probe
		uint16 type;		// ProbeType enum value
		uint16 flags;		// bit 0 = dirty (needs bake), bits 1+ = reserved
		void *payload;		// type-specific data, owned by the consumer
	};

	// Max probe count across ALL types. Sparse auto-detect typically
	// places ~150-250 for Vice City; 256 leaves a small headroom.
	// Bumping past this would need a heap-backed std::vector or similar
	// — keep it bounded so the lookup loop stays predictable.
	enum { MAX_PROBES = 256 };

	static CProbe probes[MAX_PROBES];
	static int numProbes;
	static bool bootstrapped;

	// Boostrap params — exposed for the menu (Stage 39 etc.) to tune
	// without recompiling. Defaults aim for ~100-150 reflection probes
	// in VC; smaller values trim VRAM, larger values give per-block
	// reflection accuracy at the cost of bake time + memory.
	static float MinBuildingRadius;	// metres; smaller buildings skipped
	static float GridSpacing;		// metres between grid-fill probes
	static float DedupeRadius;		// metres; probes closer than this merge

	// Engine-startup zero. Safe to call before CWorld exists.
	static void InitOnce(void);

	// Walk CWorld::ms_pBuildingPool, place probes at building centres
	// and grid-fill open space. Idempotent: clears existing probes
	// first. Call AFTER world streaming finishes (post-LoadAllRequested-
	// Models) so building bounds are populated.
	static void Bootstrap(void);

	// Lazy-bootstrap entry point safe to call every frame. Checks the
	// `bootstrapped` flag + verifies CWorld has actual buildings and
	// kicks off the scan exactly once per session. Subsequent calls
	// are a single bool comparison, so it's cheap to hang off the
	// main render loop.
	static void Update(void);

	// Stage 36 — SH (Spherical Harmonics) light probe payload. 9 SH
	// coefficients × 3 channels (RGB). Allocated once per PROBE_SH
	// entry by AllocSHPayloads(), populated by BakeSHFromSky() each
	// time of day, and queried per scene-pass via GetNearestSH() so
	// the receiver gets a location-tinted ambient term.
	struct SHPayload {
		float coeff[9][3];	// SH-2 basis; row = band-major (Y00, Y1-1, Y10, Y11, Y2-2 ... Y22)
	};
	static SHPayload shProbeData[MAX_PROBES];	// indexed by probe array slot

	// Allocate / wire up payload pointers for every PROBE_SH entry.
	// Idempotent. Called once after Bootstrap() and again after every
	// Clear(). No dynamic allocation — uses the static shProbeData
	// pool above so memory is predictable and never freed at runtime.
	static void AllocSHPayloads(void);

	// Refresh all SH probe coefficients from the current CTimeCycle
	// sky / horizon / ground colours. Cheap (~0.5ms for 200 probes)
	// — invoked by the per-frame update so dawn/dusk colour shifts
	// follow the time-cycle without a separate bake pass.
	static void BakeSHFromSky(void);

	// Find the nearest PROBE_SH probe to `worldPos` and write its 9
	// SH coefficients into outCoeffs[0..8].rgb. Returns the probe
	// index, or -1 if no SH probe is in range. outCoeffs must be a
	// float[9][3] array (9 RGB triplets, band-major).
	static int GetNearestSH(CVector worldPos, float outCoeffs[9][3]);

	// Stage 37 + 38 — Occlusion / Bent Normal probe payload. Combined
	// into a single struct because the two are always queried together
	// (the bent normal is the direction of the unoccluded region whose
	// occlusion ratio is `ao`). Bake heuristic in BakeOcclusionBentN()
	// uses building density at the probe position to drive both.
	struct OcclusionPayload {
		float ao;		// 0 = fully occluded, 1 = fully open
		CVector bentN;	// unit-ish vector pointing into the open hemisphere
	};
	static OcclusionPayload occProbeData[MAX_PROBES];

	// Allocate occlusion payload pointers + bake initial values from a
	// building-density heuristic. Fired once after Bootstrap() — the
	// values stay static across the session (unlike SH which rebakes
	// every frame because the sky changes). If a future stage adds a
	// raycast-based bake, replace BakeOcclusionBentN() with it without
	// touching the consumers.
	static void AllocOcclusionPayloads(void);
	static void BakeOcclusionBentN(void);

	// Return the nearest OCCLUSION probe's (ao, bentN) to `worldPos`.
	// Falls back to (1.0, (0,0,1)) when no probes are in range so the
	// upload is always safe.
	static int GetNearestOcclusion(CVector worldPos, float &outAO, CVector &outBentN);

	// Drop all entries. Each consumer subsystem is responsible for
	// freeing its own payload memory before this is called — the
	// manager doesn't know how to deallocate type-specific data.
	static void Clear(void);

	// Linear scan returning the top-N nearest probes to `worldPos`
	// that match the given type mask. Writes up to `maxOut` (idx,
	// weight) pairs to the caller's arrays, normalised so weights
	// sum to 1.0. Returns the actual count written. Distance falloff
	// is inverse-square within each probe's influence radius (or a
	// global default when radius == 0).
	static int FindNearestN(CVector worldPos, ProbeType type,
	                        int *outIdx, float *outWeights, int maxOut);

	// Convenience accessors for consumers.
	static int GetCount(void) { return numProbes; }
	static CProbe *GetProbe(int i) { return (i >= 0 && i < numProbes) ? &probes[i] : nullptr; }
};

#endif
