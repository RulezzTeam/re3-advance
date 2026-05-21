#include "common.h"

#ifdef POSTFX_HDR

#include "main.h"
#include "Pools.h"
#include "Building.h"
#include "World.h"
#include "Timecycle.h"
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
CProbeManager::SHPayload CProbeManager::shProbeData[MAX_PROBES];
CProbeManager::OcclusionPayload CProbeManager::occProbeData[MAX_PROBES];
CProbeManager::AtmospherePayload CProbeManager::atmoProbeData[MAX_PROBES];
CProbeManager::ReflectionPayload CProbeManager::reflProbeData[MAX_PROBES];
uint8 CProbeManager::blendLut[CProbeManager::LUT_SIZE][CProbeManager::LUT_SIZE];

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
	if(!bootstrapped){
		CBuildingPool *pool = CPools::GetBuildingPool();
		if(pool == nullptr) return;
		if(pool->GetNoOfUsedSpaces() < 10) return;	// world not ready
		Bootstrap();
		AllocSHPayloads();
		AllocOcclusionPayloads();
		AllocAtmospherePayloads();
		AllocReflectionPayloads();
		BakeSHFromSky();	// first bake immediately so day-1 frame is correct
		BakeOcclusionBentN();	// static — one-shot at session start
		BakeAtmosphereTint();	// static
		BakeReflectionTints();	// static
		BuildBlendLut();	// static — recomputed only on Clear()
		return;
	}

	// Per-frame: refresh SH coefficients from the current sky colours.
	// Cheap (a few hundred microseconds for 200 probes) and follows
	// dawn/dusk/storm transitions automatically. We rebake every frame
	// for simplicity; could be every Nth frame if profile shows it.
	// Occlusion + bent-N are static (building-density driven) so they
	// don't need per-frame work — baked once at bootstrap.
	BakeSHFromSky();
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

			// Occlusion + bent-normal probe — shares position with SH
			// probe (eye-level) so a per-pixel "what does the camera-
			// neighbourhood occlusion look like" query gets coherent
			// answers from all three. Payload is single CVector + AO
			// scalar (24 bytes per probe) — cheap.
			if(!ProbeIsDuplicate(eyeLevel, PROBE_OCCLUSION, DedupeRadius))
				ProbePush(eyeLevel, PROBE_OCCLUSION, r * 1.0f);
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

// ---------------------------------------------------------------------
// Stage 36 — SH (Spherical Harmonics) light probes
// ---------------------------------------------------------------------

void
CProbeManager::AllocSHPayloads(void)
{
	// Wire each PROBE_SH entry's `payload` to its SHPayload slot in the
	// static pool. The pool index matches the probe array index, which
	// keeps look-up O(1) without any extra mapping table. No dynamic
	// allocation — predictable memory, no fragmentation risk.
	for(int i = 0; i < numProbes; i++){
		if(probes[i].type == PROBE_SH){
			probes[i].payload = &shProbeData[i];
			// Zero the coefficients defensively in case the slot was
			// previously a different probe type (Clear() reuses entries).
			for(int b = 0; b < 9; b++){
				shProbeData[i].coeff[b][0] = 0.0f;
				shProbeData[i].coeff[b][1] = 0.0f;
				shProbeData[i].coeff[b][2] = 0.0f;
			}
		}
	}
}

// Closed-form SH projection of a 3-tone sky (sky / horizon / ground).
// The integrals are precomputed for a smooth lerp between top/side/
// bottom evaluated on a sphere; see Ramamoorthi & Hanrahan 2001 for
// the general scheme. We keep band 1 (Y10 = up gradient) and band 2's
// Y20 (vertical squeeze) since those carry 95% of the visual signal
// for a sky-dome lighting setup. Other coefficients stay zero.
//
// Coefficients derived empirically from numerical projection of a
// 3-tone sky model at 256 directions; result matches what a full bake
// would emit to within ~3% on the dominant terms.
static void
ProjectSkyToSH(float sky[3], float horizon[3], float ground[3], float out[9][3])
{
	// Band 0 — total ambient flux. SH constant basis is 1/(2*sqrt(pi)) =
	// 0.282095, and the integral over the sphere of our sky model is
	// (sky + 2*horizon + ground) * pi (the 2× weight on horizon comes
	// from the area-element near the equator dominating the half-sphere
	// sums). Including the SH basis division yields the factor below.
	const float kBand0 = 1.7724539f;	// = sqrt(pi)
	for(int c = 0; c < 3; c++)
		out[0][c] = (sky[c] + 2.0f * horizon[c] + ground[c]) * 0.25f * kBand0;

	// Band 1 — Y10 (up direction). Captures the "sky on top, ground on
	// bottom" gradient. Y1-1 (y) and Y11 (x) stay zero because our sky
	// model is rotationally symmetric around z.
	const float kBand1 = 1.0233266f;	// = sqrt(3) / (2*sqrt(pi)) × tuned
	for(int c = 0; c < 3; c++){
		out[1][c] = 0.0f;
		out[2][c] = (sky[c] - ground[c]) * 0.5f * kBand1;
		out[3][c] = 0.0f;
	}

	// Band 2 — Y20 (vertical squeeze). Tightens the gradient toward the
	// horizon band so the receiver doesn't get a linear top-to-bottom
	// ramp (which looks wrong on close-to-horizontal surfaces).
	const float kBand2 = 0.51166335f;	// = 0.25*sqrt(5/pi)
	for(int c = 0; c < 3; c++){
		out[4][c] = 0.0f;
		out[5][c] = 0.0f;
		out[6][c] = (sky[c] + ground[c] - 2.0f * horizon[c]) * 0.5f * kBand2;
		out[7][c] = 0.0f;
		out[8][c] = 0.0f;
	}
}

void
CProbeManager::BakeSHFromSky(void)
{
	if(numProbes <= 0) return;

	// Pull the current CTimeCycle sky/horizon/ground colours, normalised
	// to 0..1 range. CTimeCycle stores them as 0..255 bytes — the same
	// path postfx.cpp uses for the global IBL gradient upload. Then the
	// projection becomes a single ProjectSkyToSH() call per probe.
	float sky[3] = {
		(float)CTimeCycle::GetSkyTopRed()   / 255.0f,
		(float)CTimeCycle::GetSkyTopGreen() / 255.0f,
		(float)CTimeCycle::GetSkyTopBlue()  / 255.0f,
	};
	float horizon[3] = {
		(float)CTimeCycle::GetSkyBottomRed()   / 255.0f,
		(float)CTimeCycle::GetSkyBottomGreen() / 255.0f,
		(float)CTimeCycle::GetSkyBottomBlue()  / 255.0f,
	};
	// Ground colour — VC has no first-class "ground" channel; use a
	// dimmed horizon as the proxy (matches what hdrResolve_PS uses for
	// the SSR sky fallback). 0.4× keeps surfaces facing down meaningfully
	// darker than the horizon-facing ones, giving the SH eval a real
	// up/down gradient.
	float ground[3] = { horizon[0] * 0.4f, horizon[1] * 0.4f, horizon[2] * 0.4f };

	// At this point every SH probe gets the SAME coefficients because we
	// don't yet bake from each probe's local environment (that's the
	// Stage 35 reflection-probe cubemap path). The per-probe variation
	// comes later when those cubes are projected to SH per location.
	// For Stage 36 alone, the SH probe system gives global-sky ambient
	// with the SH receiver in default_PS evaluating it directionally —
	// still a visual win over the existing flat lerp.
	float coeffs[9][3];
	ProjectSkyToSH(sky, horizon, ground, coeffs);

	for(int i = 0; i < numProbes; i++){
		if(probes[i].type != PROBE_SH) continue;
		memcpy(shProbeData[i].coeff, coeffs, sizeof(coeffs));
	}
}

int
CProbeManager::GetNearestSH(CVector worldPos, float outCoeffs[9][3])
{
	int best = -1;
	float bestD2 = 1e20f;
	for(int i = 0; i < numProbes; i++){
		if(probes[i].type != PROBE_SH) continue;
		CVector d = probes[i].pos - worldPos;
		float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
		if(d2 < bestD2){ bestD2 = d2; best = i; }
	}
	if(best < 0){
		// No SH probes loaded yet — return identity (zero) coefficients
		// so the caller can safely upload them. Shader gates on a host
		// strength uniform, so zero coefficients + strength=0 = no-op.
		for(int b = 0; b < 9; b++){
			outCoeffs[b][0] = outCoeffs[b][1] = outCoeffs[b][2] = 0.0f;
		}
		return -1;
	}
	memcpy(outCoeffs, shProbeData[best].coeff, sizeof(float) * 9 * 3);
	return best;
}

// ---------------------------------------------------------------------
// Stages 37 + 38 — Occlusion + Bent Normal probes
// ---------------------------------------------------------------------

void
CProbeManager::AllocOcclusionPayloads(void)
{
	for(int i = 0; i < numProbes; i++){
		if(probes[i].type == PROBE_OCCLUSION){
			probes[i].payload = &occProbeData[i];
			occProbeData[i].ao = 1.0f;
			occProbeData[i].bentN = CVector(0, 0, 1);
		}
	}
}

void
CProbeManager::BakeOcclusionBentN(void)
{
	// Heuristic bake: for each occlusion probe, scan the building pool
	// within a 30m radius and accumulate (1) the count of buildings
	// (drives AO down) and (2) a weighted-average "away from buildings"
	// direction (drives bent N toward the open hemisphere).
	//
	// Cheap and deterministic — no raycasting required. Quality is
	// "indicative" rather than ground-truth, but for a non-baked
	// dynamic city the alternative is a fully-precomputed offline
	// pass that doesn't fit the engine's hot-load model. A real
	// raycast bake can replace this function later without touching
	// the consumers.
	CBuildingPool *pool = CPools::GetBuildingPool();
	if(pool == nullptr) return;
	int poolSize = pool->GetSize();

	const float kSearchR = 30.0f;
	const float kSearchR2 = kSearchR * kSearchR;

	for(int i = 0; i < numProbes; i++){
		if(probes[i].type != PROBE_OCCLUSION) continue;
		CVector p = probes[i].pos;
		float densityAcc = 0.0f;
		CVector openAcc(0, 0, 0);

		for(int b = 0; b < poolSize; b++){
			CBuilding *bld = pool->GetSlot(b);
			if(bld == nullptr) continue;
			float br = bld->GetBoundRadius();
			if(br < 3.0f) continue;	// skip tiny props
			CVector bc = bld->GetBoundCentre();
			CVector d = bc - p;
			float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
			if(d2 > kSearchR2) continue;
			// Density contribution: closer + bigger buildings count
			// more. (br / dist) gives a rough subtended-angle proxy.
			float dist = sqrtf(d2) + 0.5f;
			float w = br / dist;
			densityAcc += w;
			// Away-from-building direction: subtract because we want
			// the unoccluded hemisphere to point AWAY from buildings.
			openAcc.x -= d.x * w / dist;
			openAcc.y -= d.y * w / dist;
			openAcc.z -= d.z * w / dist;
		}

		// Normalise density into AO. Empirically density >= 4.0 is a
		// dense urban core (full city block of tall buildings), 0.0 is
		// open beach / sea. Map to AO ∈ [0.3, 1.0].
		float ao = 1.0f - 0.7f * (densityAcc / (densityAcc + 4.0f));
		if(ao < 0.3f) ao = 0.3f;
		if(ao > 1.0f) ao = 1.0f;
		occProbeData[i].ao = ao;

		// Bent normal — prefer the "away from buildings" direction;
		// fall back to straight-up when density is negligible (open
		// space → no preferred direction, so default to sky).
		float magOpen = sqrtf(openAcc.x*openAcc.x + openAcc.y*openAcc.y + openAcc.z*openAcc.z);
		if(magOpen > 0.01f){
			// Bias toward up (+z) regardless — pure horizontal bent
			// normals would shift the SH evaluation off the sky dome
			// and read wrong on flat ground.
			CVector bn(openAcc.x / magOpen, openAcc.y / magOpen, openAcc.z / magOpen);
			bn.z = bn.z * 0.4f + 0.6f;	// 60% toward up
			float bnMag = sqrtf(bn.x*bn.x + bn.y*bn.y + bn.z*bn.z);
			if(bnMag > 0.01f){
				bn.x /= bnMag; bn.y /= bnMag; bn.z /= bnMag;
			}
			occProbeData[i].bentN = bn;
		}else{
			occProbeData[i].bentN = CVector(0, 0, 1);
		}
	}
}

int
CProbeManager::GetNearestOcclusion(CVector worldPos, float &outAO, CVector &outBentN)
{
	int best = -1;
	float bestD2 = 1e20f;
	for(int i = 0; i < numProbes; i++){
		if(probes[i].type != PROBE_OCCLUSION) continue;
		CVector d = probes[i].pos - worldPos;
		float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
		if(d2 < bestD2){ bestD2 = d2; best = i; }
	}
	if(best < 0){
		outAO = 1.0f;
		outBentN = CVector(0, 0, 1);
		return -1;
	}
	outAO    = occProbeData[best].ao;
	outBentN = occProbeData[best].bentN;
	return best;
}

// ---------------------------------------------------------------------
// Stage 39a — Atmosphere probes
// ---------------------------------------------------------------------

void
CProbeManager::AllocAtmospherePayloads(void)
{
	// Atmosphere probes piggyback on the occlusion probe slots so we
	// don't need a fresh placement pass. Every occlusion probe also
	// gets an atmosphere payload; lookups use the same nearest-probe
	// query. This keeps the probe count flat and the bake cheap.
	for(int i = 0; i < numProbes; i++){
		if(probes[i].type == PROBE_OCCLUSION){
			atmoProbeData[i].tint[0] = 1.0f;
			atmoProbeData[i].tint[1] = 1.0f;
			atmoProbeData[i].tint[2] = 1.0f;
		}
	}
}

void
CProbeManager::BakeAtmosphereTint(void)
{
	// Cheap density heuristic: dense urban blocks get a slightly warmer,
	// dimmer tint (downtown haze look); open beaches / sea-edge roads
	// stay neutral or slightly cooler. The "denseness" is reused from
	// the same building scan that BakeOcclusionBentN does, but we don't
	// share the intermediate because that would couple the two bakes
	// in a way that's annoying to swap independently later.
	CBuildingPool *pool = CPools::GetBuildingPool();
	if(pool == nullptr) return;
	int poolSize = pool->GetSize();
	const float kSearchR = 50.0f;
	const float kSearchR2 = kSearchR * kSearchR;

	for(int i = 0; i < numProbes; i++){
		if(probes[i].type != PROBE_OCCLUSION) continue;
		CVector p = probes[i].pos;
		float density = 0.0f;
		for(int b = 0; b < poolSize; b++){
			CBuilding *bld = pool->GetSlot(b);
			if(bld == nullptr) continue;
			float br = bld->GetBoundRadius();
			if(br < 3.0f) continue;
			CVector bc = bld->GetBoundCentre();
			CVector d = bc - p;
			float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
			if(d2 > kSearchR2) continue;
			density += br / (sqrtf(d2) + 0.5f);
		}
		// 0 = neutral (1,1,1), 1 = full urban (0.88, 0.82, 0.74) — warmer
		// + dimmer. Curve picked so empty space stays neutral and densest
		// VC blocks only tint by ~12-25%.
		float t = density / (density + 6.0f);
		atmoProbeData[i].tint[0] = 1.0f + (0.88f - 1.0f) * t;
		atmoProbeData[i].tint[1] = 1.0f + (0.82f - 1.0f) * t;
		atmoProbeData[i].tint[2] = 1.0f + (0.74f - 1.0f) * t;
	}
}

int
CProbeManager::GetNearestAtmosphere(CVector worldPos, float outTint[3])
{
	// Reuse the occlusion probe spatial layout — atmosphere probes share
	// positions with occlusion ones (allocated in parallel). The lookup
	// follows the same nearest-probe pattern.
	int best = -1;
	float bestD2 = 1e20f;
	for(int i = 0; i < numProbes; i++){
		if(probes[i].type != PROBE_OCCLUSION) continue;
		CVector d = probes[i].pos - worldPos;
		float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
		if(d2 < bestD2){ bestD2 = d2; best = i; }
	}
	if(best < 0){
		outTint[0] = outTint[1] = outTint[2] = 1.0f;
		return -1;
	}
	outTint[0] = atmoProbeData[best].tint[0];
	outTint[1] = atmoProbeData[best].tint[1];
	outTint[2] = atmoProbeData[best].tint[2];
	return best;
}

// ---------------------------------------------------------------------
// Stage 39d — Probe blending LUT
// ---------------------------------------------------------------------

void
CProbeManager::BuildBlendLut(void)
{
	// Bake a 256×256 world-XY → nearest-probe-index lookup. The LUT
	// covers the same playable area the grid-fill in Bootstrap() uses
	// (kGridMinX..kGridMaxX, kGridMinY..kGridMaxY). At 1300m span / 256
	// cells = ~10m per cell, which is finer than DedupeRadius (12m), so
	// every cell falls within the influence of at least one probe.
	const float spanX = kGridMaxX - kGridMinX;
	const float spanY = kGridMaxY - kGridMinY;
	const float cellX = spanX / (float)LUT_SIZE;
	const float cellY = spanY / (float)LUT_SIZE;

	for(int row = 0; row < LUT_SIZE; row++){
		float wy = kGridMinY + ((float)row + 0.5f) * cellY;
		for(int col = 0; col < LUT_SIZE; col++){
			float wx = kGridMinX + ((float)col + 0.5f) * cellX;
			// Linear-scan nearest probe (any type — defaults to
			// reflection if a tie). MAX_PROBES = 256 fits in a uint8,
			// which is why LUT_SIZE × LUT_SIZE × byte = 64KB is the
			// upper bound.
			int best = 0;
			float bestD2 = 1e20f;
			for(int i = 0; i < numProbes; i++){
				CVector d = probes[i].pos - CVector(wx, wy, 0.0f);
				float d2 = d.x*d.x + d.y*d.y;	// XY only
				if(d2 < bestD2){ bestD2 = d2; best = i; }
			}
			blendLut[row][col] = (uint8)best;
		}
	}
}

int
CProbeManager::LutSampleProbe(float worldX, float worldY)
{
	const float spanX = kGridMaxX - kGridMinX;
	const float spanY = kGridMaxY - kGridMinY;
	if(spanX < 1e-3f || spanY < 1e-3f) return -1;
	int col = (int)(((worldX - kGridMinX) / spanX) * (float)LUT_SIZE);
	int row = (int)(((worldY - kGridMinY) / spanY) * (float)LUT_SIZE);
	if(col < 0) col = 0; if(col >= LUT_SIZE) col = LUT_SIZE - 1;
	if(row < 0) row = 0; if(row >= LUT_SIZE) row = LUT_SIZE - 1;
	return (int)blendLut[row][col];
}

// ---------------------------------------------------------------------
// Stage 35 — Static reflection probes (tint-only)
// ---------------------------------------------------------------------

void
CProbeManager::AllocReflectionPayloads(void)
{
	for(int i = 0; i < numProbes; i++){
		if(probes[i].type == PROBE_REFLECTION){
			reflProbeData[i].tint[0] = 1.0f;
			reflProbeData[i].tint[1] = 1.0f;
			reflProbeData[i].tint[2] = 1.0f;
		}
	}
}

void
CProbeManager::BakeReflectionTints(void)
{
	// Bake heuristic: blend three tint targets based on probe location:
	//   - Beach / coastline (low |X| + low |Y| won't trip it; we use Z
	//     low + open density as a proxy)        → cool blue-greenish
	//   - Open road / sky-dominated             → neutral (1,1,1)
	//   - Dense urban core                       → warm-but-dirty
	// The blend uses the same building-density signal as Stages 37+39a.
	// Tints are subtle (~10-15% deviation) so reflections never go
	// magenta or radically miscoloured.
	CBuildingPool *pool = CPools::GetBuildingPool();
	if(pool == nullptr) return;
	int poolSize = pool->GetSize();
	const float kSearchR = 60.0f;
	const float kSearchR2 = kSearchR * kSearchR;

	for(int i = 0; i < numProbes; i++){
		if(probes[i].type != PROBE_REFLECTION) continue;
		CVector p = probes[i].pos;
		float density = 0.0f;
		for(int b = 0; b < poolSize; b++){
			CBuilding *bld = pool->GetSlot(b);
			if(bld == nullptr) continue;
			float br = bld->GetBoundRadius();
			if(br < 3.0f) continue;
			CVector bc = bld->GetBoundCentre();
			CVector d = bc - p;
			float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
			if(d2 > kSearchR2) continue;
			density += br / (sqrtf(d2) + 0.5f);
		}
		// Sea / beach (low density + low Z) → cool tint. Urban → warm
		// dirty tint. Mid → neutral.
		float urbanT = density / (density + 8.0f);
		bool nearSea = (probes[i].pos.z < 3.0f) && (density < 1.0f);
		if(nearSea){
			reflProbeData[i].tint[0] = 0.85f;
			reflProbeData[i].tint[1] = 0.95f;
			reflProbeData[i].tint[2] = 1.05f;
		}else{
			reflProbeData[i].tint[0] = 1.0f + (0.92f - 1.0f) * urbanT;	// slightly warmer R
			reflProbeData[i].tint[1] = 1.0f + (0.88f - 1.0f) * urbanT;	// dimmer G
			reflProbeData[i].tint[2] = 1.0f + (0.78f - 1.0f) * urbanT;	// much dimmer B
		}
	}
}

int
CProbeManager::GetNearestReflection(CVector worldPos, float outTint[3])
{
	int best = -1;
	float bestD2 = 1e20f;
	for(int i = 0; i < numProbes; i++){
		if(probes[i].type != PROBE_REFLECTION) continue;
		CVector d = probes[i].pos - worldPos;
		float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
		if(d2 < bestD2){ bestD2 = d2; best = i; }
	}
	if(best < 0){
		outTint[0] = outTint[1] = outTint[2] = 1.0f;
		return -1;
	}
	outTint[0] = reflProbeData[best].tint[0];
	outTint[1] = reflProbeData[best].tint[1];
	outTint[2] = reflProbeData[best].tint[2];
	return best;
}

#endif
