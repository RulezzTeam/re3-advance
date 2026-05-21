#include "common.h"

#ifdef POSTFX_HDR

#include "main.h"
#include "Timer.h"
#include "decals.h"

CDecals::Decal CDecals::decals[CDecals::MAX_DECALS];
int CDecals::writeCursor = 0;
bool CDecals::Enabled = false;
float CDecals::MaxAgeSeconds = 30.0f;

// Shader handle reserved for Stage 15.2 splat pass — loaded lazily on
// first call to Render() once that pass is wired in. Kept as a static
// so the (currently stub) Render() implementation can be replaced
// without touching the header.
#ifdef RW_D3D9
static void *decals_PS = nil;
#endif

void
CDecals::InitOnce(void)
{
	writeCursor = 0;
	for(int i = 0; i < MAX_DECALS; i++){
		decals[i].pos = CVector(0, 0, 0);
		decals[i].radius = 0.0f;
		decals[i].r = decals[i].g = decals[i].b = 1.0f;
		decals[i].ageMs = 0.0f;
		decals[i].type = BULLET_HOLE;
		decals[i].inUse = 0;
	}
}

void
CDecals::Add(CVector pos, CVector /*normal*/, Type type, float radiusOverride)
{
	// Pick a slot — overwrite the oldest entry to keep MAX_DECALS as
	// a hard cap. writeCursor wraps so the search is O(1).
	Decal &d = decals[writeCursor];
	writeCursor = (writeCursor + 1) % MAX_DECALS;

	d.pos = pos;
	d.ageMs = 0.0f;
	d.type = (uint8)type;
	d.inUse = 1;

	// Per-type defaults — radius + base colour. The colour is HDR-safe
	// (≤1) so the shader's additive blend doesn't blow out brightness.
	switch(type){
	case BULLET_HOLE:
		d.radius = (radiusOverride > 0.0f) ? radiusOverride : 0.20f;
		d.r = 0.05f; d.g = 0.04f; d.b = 0.03f;	// near-black, slight warmth
		break;
	case BLOOD:
		d.radius = (radiusOverride > 0.0f) ? radiusOverride : 0.60f;
		d.r = 0.42f; d.g = 0.05f; d.b = 0.05f;
		break;
	case PAINT:
		d.radius = (radiusOverride > 0.0f) ? radiusOverride : 0.80f;
		d.r = 0.80f; d.g = 0.70f; d.b = 0.20f;	// caution yellow default
		break;
	case OIL:
		d.radius = (radiusOverride > 0.0f) ? radiusOverride : 1.20f;
		d.r = 0.02f; d.g = 0.02f; d.b = 0.02f;	// black slick
		break;
	}
}

void
CDecals::Update(void)
{
	float dtMs = CTimer::GetTimeStepNonClipped() * (1000.0f / 50.0f);
	float maxMs = MaxAgeSeconds * 1000.0f;
	for(int i = 0; i < MAX_DECALS; i++){
		if(!decals[i].inUse) continue;
		decals[i].ageMs += dtMs;
		if(decals[i].ageMs > maxMs)
			decals[i].inUse = 0;
	}
}

void
CDecals::Render(RwCamera *cam)
{
	// Stage 15 v1 — infrastructure only. The decal *splat* pass is not
	// wired in yet because writing additively to pHdrScene between
	// EndScenePass and ResolveHDR needs a separate camera bind path
	// that's worth its own dedicated review. For now this is a stub so
	// the CDecals::Add() API can land + game code can start spawning
	// decals; the visual rendering will come in a follow-up Stage 15.2.
	//
	// All the data is here (decals[] array, Update() ages out entries),
	// so when the splat pass lands no game-side migration is needed.
	(void)cam;
	(void)decals_PS;
}

#endif
