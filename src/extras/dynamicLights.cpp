#define WITHD3D
#include "common.h"

#ifdef POSTFX_HDR

#ifndef LIBRW
#error "POSTFX_HDR needs librw"
#endif

#include "main.h"
#include "Camera.h"
#include "PointLights.h"
#include "dynamicLights.h"

// librw — uses Librw on include path via premake (`includedirs { Librw }`)
#include "src/rwlog.h"

bool CDynamicLights::Enabled = true;
float CDynamicLights::Intensity = 1.0f;
float CDynamicLights::Reach = 1.0f;
int CDynamicLights::MaxLights = CDynamicLights::MAX_LIGHTS;

void
CDynamicLights::InitOnce(void)
{
	// Defaults live in the class member initialisers above; nothing
	// dynamic to set up. Function exists to mirror the other modules'
	// InitOnce/Open/Close pattern.
}

namespace {
struct ScoredLight {
	int   idx;	// index into CPointLights::aLights[]
	float score;	// luminance / dist²
	float lum;
};
}

void
CDynamicLights::Update(RwCamera *cam)
{
	if(!Enabled){
		// Zero count → shader [loop] short-circuits.
		float zeroPos[MAX_LIGHTS][3] = {0}; float zeroR[MAX_LIGHTS] = {0};
		float zeroCol[MAX_LIGHTS][3] = {0}; float zeroI[MAX_LIGHTS] = {0};
		rw::d3d::setDynamicPointLights(0, zeroPos, zeroR, zeroCol, zeroI);
		rw::d3d::uploadDynamicPointLights();
		return;
	}

	int activeCount = MaxLights;
	if(activeCount < 0) activeCount = 0;
	if(activeCount > MAX_LIGHTS) activeCount = MAX_LIGHTS;

	// Score every registered point light by luminance × inverse-distance².
	// LIGHT_DIRECTIONAL types (headlights are registered as directional in
	// the original re3 code, see Automobile.cpp:2455) and LIGHT_POINT
	// both get picked — the shader treats them identically since we
	// only forward position + radius + colour to it.
	rw::Camera *sceneCam = (rw::Camera*)cam;
	if(sceneCam == nullptr || CPointLights::NumLights == 0){
		float zeroPos[MAX_LIGHTS][3] = {0}; float zeroR[MAX_LIGHTS] = {0};
		float zeroCol[MAX_LIGHTS][3] = {0}; float zeroI[MAX_LIGHTS] = {0};
		rw::d3d::setDynamicPointLights(0, zeroPos, zeroR, zeroCol, zeroI);
		rw::d3d::uploadDynamicPointLights();
		return;
	}
	rw::V3d camPosRw = sceneCam->getFrame()->getLTM()->pos;
	CVector camPos(camPosRw.x, camPosRw.y, camPosRw.z);

	ScoredLight scored[NUMPOINTLIGHTS];
	int numScored = 0;
	for(int i = 0; i < CPointLights::NumLights; i++){
		const CRegisteredPointLight &L = CPointLights::aLights[i];
		// Skip non-lit types (fog-only / darken don't contribute light).
		if(L.type == CPointLights::LIGHT_DARKEN ||
		   L.type == CPointLights::LIGHT_FOGONLY ||
		   L.type == CPointLights::LIGHT_FOGONLY_ALWAYS)
			continue;
		float lum = L.red + L.green + L.blue;
		if(lum < 0.01f) continue;
		CVector d = L.coors - camPos;
		float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
		if(d2 < 0.01f) d2 = 0.01f;
		// Score is luminance × inverse-distance² so a dim faraway lamp
		// loses to a bright nearby headlight.
		float score = lum / d2;
		scored[numScored].idx   = i;
		scored[numScored].score = score;
		scored[numScored].lum   = lum;
		numScored++;
	}

	// Selection sort — N is tiny (≤NUMPOINTLIGHTS=128, almost always <30),
	// and we only need the top `activeCount` so we can stop early.
	if(numScored > activeCount){
		for(int i = 0; i < activeCount; i++){
			int best = i;
			for(int j = i + 1; j < numScored; j++)
				if(scored[j].score > scored[best].score)
					best = j;
			if(best != i){
				ScoredLight t = scored[i];
				scored[i] = scored[best];
				scored[best] = t;
			}
		}
		numScored = activeCount;
	}else if(numScored > activeCount){
		numScored = activeCount;
	}

	// Pack into the format librw expects. MAX_LIGHTS-sized arrays match
	// what librw expects on its side (DYN_LIGHT_SLOTS = MAX_LIGHTS = 32).
	float positions[MAX_LIGHTS][3]  = {0};
	float radii[MAX_LIGHTS]         = {0};
	float colours[MAX_LIGHTS][3]    = {0};
	float intensities[MAX_LIGHTS]   = {0};
	int outCount = numScored < activeCount ? numScored : activeCount;
	for(int i = 0; i < outCount; i++){
		const CRegisteredPointLight &L = CPointLights::aLights[scored[i].idx];
		positions[i][0] = L.coors.x;
		positions[i][1] = L.coors.y;
		positions[i][2] = L.coors.z;
		// Reach multiplies the engine-registered radius — useful for
		// pushing headlights further onto building facades. Clamped so
		// the shader's distance loop doesn't waste cycles on huge balls.
		float r = L.radius * Reach;
		if(r < 0.5f) r = 0.5f;
		if(r > 80.0f) r = 80.0f;
		radii[i] = r;
		colours[i][0] = L.red;
		colours[i][1] = L.green;
		colours[i][2] = L.blue;
		intensities[i] = Intensity;
	}

	rw::d3d::setDynamicPointLights(outCount, positions, radii, colours, intensities);
	rw::d3d::uploadDynamicPointLights();
}

#endif
