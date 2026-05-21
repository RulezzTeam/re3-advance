#include "common.h"

#ifdef POSTFX_HDR

#include "main.h"
#include "Pools.h"
#include "Building.h"
#include "World.h"
#include "probeManager.h"

// Stage 34 — Probe Manager implementation. See probeManager.h for the
// design rationale. The bootstrap algorithm is intentionally simple:
//   1. Walk every building in CPools::GetBuildingPool(); keep the ones
//      with bound radius ≥ MinBuildingRadius. Place a reflection probe
//      at the building centre + an SH probe slightly above (1.5 m up)
//      to capture indoor-ish ambient.
//   2. Grid-fill the playable area (~ a 1.5km × 1.5km box covering VC)
//      at GridSpacing intervals, dropping any candidate within
//      DedupeRadius of an existing probe.
//   3. Cap the result at MAX_PROBES; oversized scenes lose the
//      lowest-priority entries (last-in grid-fill samples).

CProbeManager::CProbe CProbeManager::probes[MAX_PROBES];
int CProbeManager::numProbes = 0;
bool CProbeManager::bootstrapped = false;

float CProbeManager::MinBuildingRadius = 10.0f;	// metres
float CProbeManager::GridSpacing = 80.0f;		// metres
float CProbeManager::DedupeRadius = 12.0f;		// metres

// VC bounds approximation. The playable area is ~−2000..+2000 in x/y,
// but a tighter box keeps probe count manageable. These are the limits
// the grid-fill sweep uses; clamp to actual world bounds if the values
// here ever drift.
static const float kGridMinX = -1300.0f;
static const float kGridMaxX =  1300.0f;
static const float kGridMinY = -1300.0f;
static const float kGridMaxY =  1300.0f;
static const float kGridZ    =  10.0f;	// eye-height fallback; refined below

void
CProbeManager::InitOnce(void)
{
	numProbes = 0;
	bootstrapped = false;
	for(int i = 0; i < MAX_PROBES; i++){
		probes[i].pos = CVector(0, 0, 0);
		probes[i].radius = 0.0f;
		probes[i].type = PROBE_REFLECTION;
		probes[i].flags = 0;
		probes[i].payload = nullptr;
	}
}

void
CProbeManager::Clear(void)
{
	// Payload memory is owned by per-type subsystems (Stages 35-39).
	// They MUST free their payloads before this runs, or leak. We zero
	// out the entries here so a re-bootstrap starts from a clean slate.
	numProbes = 0;
	bootstrapped = false;
	for(int i = 0; i < MAX_PROBES; i++){
		probes[i].pos = CVector(0, 0, 0);
		probes[i].radius = 0.0f;
		probes[i].type = PROBE_REFLECTION;
		probes[i].flags = 0;
		probes[i].payload = nullptr;
	}
}

// Internal helper — returns true if `pos` is within DedupeRadius of any
// existing probe of the same type.
static bool
ProbeIsDuplicate(const CVector &pos, CProbeManager::ProbeType type, float radius)
{
	float r2 = radius * radius;
	for(int i = 0; i < CProbeManager::numProbes; i++){
		const CProbeManager::CProbe &p = CProbeManager::probes[i];
		if(p.type != type) continue;
		CVector d = p.pos - pos;
		if(d.x*d.x + d.y*d.y + d.z*d.z < r2)
			return true;
	}
	return false;
}

// Append (no dedupe, internal use). Returns false if the table is full.
static bool
ProbePush(const CVector &pos, CProbeManager::ProbeType type, float influenceRadius)
{
	if(CProbeManager::numProbes >= CProbeManager::MAX_PROBES)
		return false;
	CProbeManager::CProbe &p = CProbeManager::probes[CProbeManager::numProbes++];
	p.pos = pos;
	p.radius = influenceRadius;
	p.type = (uint16)type;
	p.flags = 1;	// dirty — needs first-time bake
	p.payload = nullptr;
	return true;
}

void
CProbeManager::Update(void)
{
	// Lazy bootstrap — runs once when CWorld first has buildings. We
	// can't safely fire Bootstrap() from CGame::Initialise() without
	// risking it running before the building pool is populated; this
	// defers to "first frame the world looks populated". A pool size
	// of 0 means streaming hasn't loaded anything yet; bail and try
	// again next frame.
	if(bootstrapped) return;
	CBuildingPool *pool = CPools::GetBuildingPool();
	if(pool == nullptr) return;
	if(pool->GetNoOfUsedSpaces() < 10) return;	// world not ready
	Bootstrap();
}

void
CProbeManager::Bootstrap(void)
{
	// Idempotent: clear before re-populate so save-load / level change
	// doesn't double-register. Callers should ensure any per-type
	// payload memory is freed FIRST — manager doesn't know about that.
	Clear();

	// --- Stage 1: building scan ---------------------------------------
	// CPools::GetBuildingPool() returns the pool of static-world entities
	// (buildings + props with collision). For each entity whose bound
	// radius is ≥ MinBuildingRadius, place 1 reflection probe at the
	// building centre and 1 SH probe slightly above (~1.5m) to catch
	// the ambient that a pedestrian-eye-level shader would see.
	CBuildingPool *pool = CPools::GetBuildingPool();
	if(pool != nullptr){
		int poolSize = pool->GetSize();
		for(int i = 0; i < poolSize && numProbes < MAX_PROBES; i++){
			CBuilding *b = pool->GetSlot(i);
			if(b == nullptr) continue;

			float r = b->GetBoundRadius();
			if(r < MinBuildingRadius) continue;
			CVector c = b->GetBoundCentre();

			// Reflection probe at centre — influence radius = bound
			// radius × 1.5 so neighbouring buildings can blend at
			// the boundary.
			if(!ProbeIsDuplicate(c, PROBE_REFLECTION, DedupeRadius))
				ProbePush(c, PROBE_REFLECTION, r * 1.5f);

			// SH light probe at eye-level above centre. SH is cheap
			// (27 floats) so we afford a denser sampling than
			// reflections — this gives interior rooms / arcades
			// usable indirect even when the camera's reflection
			// probe is outside.
			CVector eyeLevel = c;
			eyeLevel.z += 1.5f;
			if(!ProbeIsDuplicate(eyeLevel, PROBE_SH, DedupeRadius))
				ProbePush(eyeLevel, PROBE_SH, r * 1.0f);
		}
	}

	// --- Stage 2: open-space grid fill --------------------------------
	// Sweep the playable area at GridSpacing intervals. Each candidate
	// is rejected if it lands within DedupeRadius of an existing probe
	// (so we don't double-cover building-dense areas). The Z is fixed
	// at eye-level; the actual probe will use a CWorld raycast at
	// bake-time to find a sensible ground height (Stage 35+).
	for(float y = kGridMinY; y <= kGridMaxY && numProbes < MAX_PROBES; y += GridSpacing){
		for(float x = kGridMinX; x <= kGridMaxX && numProbes < MAX_PROBES; x += GridSpacing){
			CVector p(x, y, kGridZ);
			if(!ProbeIsDuplicate(p, PROBE_REFLECTION, DedupeRadius))
				ProbePush(p, PROBE_REFLECTION, GridSpacing * 0.75f);
			// Grid SH probes too — cheap, fills sky-tinted ambient
			// in open areas (beach, parks, sea-edge roads).
			if(numProbes < MAX_PROBES &&
			   !ProbeIsDuplicate(p, PROBE_SH, DedupeRadius))
				ProbePush(p, PROBE_SH, GridSpacing * 0.75f);
		}
	}

	bootstrapped = true;
}

int
CProbeManager::FindNearestN(CVector worldPos, ProbeType type,
                            int *outIdx, float *outWeights, int maxOut)
{
	if(maxOut <= 0 || outIdx == nullptr || outWeights == nullptr)
		return 0;

	// Linear scan — N is small (typically 4..8 results from a 100-200
	// probe pool). A k-d tree would be faster but the constant-factor
	// overhead isn't worth it at this scale; the lookup runs once per
	// scene pass, not per pixel.
	for(int k = 0; k < maxOut; k++){
		outIdx[k] = -1;
		outWeights[k] = 0.0f;
	}
	float bestDist[16];	// enough for any reasonable maxOut
	if(maxOut > 16) maxOut = 16;
	for(int k = 0; k < maxOut; k++)
		bestDist[k] = 1e20f;

	for(int i = 0; i < numProbes; i++){
		const CProbe &p = probes[i];
		if(p.type != (uint16)type) continue;
		CVector d = p.pos - worldPos;
		float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
		// Worst-slot replacement — keeps the array sorted descending
		// by distance² so each iteration costs O(maxOut). Good enough
		// since maxOut ≤ 16.
		int worst = 0;
		for(int k = 1; k < maxOut; k++)
			if(bestDist[k] > bestDist[worst]) worst = k;
		if(d2 < bestDist[worst]){
			bestDist[worst] = d2;
			outIdx[worst] = i;
		}
	}

	// Convert distances to inverse-square weights, normalise to sum=1.
	float total = 0.0f;
	int count = 0;
	for(int k = 0; k < maxOut; k++){
		if(outIdx[k] < 0){ outWeights[k] = 0.0f; continue; }
		float w = 1.0f / (bestDist[k] + 0.01f);
		outWeights[k] = w;
		total += w;
		count++;
	}
	if(total > 0.0001f){
		float invTotal = 1.0f / total;
		for(int k = 0; k < maxOut; k++)
			outWeights[k] *= invTotal;
	}
	return count;
}

#endif
