#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define WITH_D3D
#include "../rwbase.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"
#include "rwd3d.h"

namespace rw {
namespace d3d {

#ifdef RW_D3D9
IDirect3DDevice9 *d3ddevice = nil;

#define MAX_LIGHTS 8


#define VS_NAME g_vs30_main
#define PS_NAME g_ps30_main
void *default_amb_VS;
void *default_amb_dir_VS;
void *default_all_VS;
void *default_PS;
void *default_tex_PS;
void *im2d_VS;
void *im2d_PS;
void *im2d_tex_PS;

bool perPixelLightingEnabled = false;
void *default_pp_amb_VS;
void *default_pp_amb_dir_VS;
void *default_pp_all_VS;
void *default_pp_PS;
void *default_pp_tex_PS;

bool gbufferEnabled = false;
void *default_pp_gbuf_amb_VS;
void *default_pp_gbuf_amb_dir_VS;
void *default_pp_gbuf_all_VS;
void *default_pp_gbuf_PS;
void *default_pp_gbuf_tex_PS;

// CSM depth-only pass — host (CCSM) flips this on while rendering each
// cascade's shadow map. defaultRenderCB_Shader and skinRenderCB use it to
// switch to a slim VS/PS pair that emits only orthographic depth, and to
// skip every material/texture upload that the colour passes do.
bool shadowDepthOnly = false;
void *shadow_VS;	// host-owned, set by CCSM::Open
void *shadow_PS;
void *shadow_skin_VS;	// optional — skinned-only fallback if non-null
float shadowLightViewProj[16];	// uploaded to VS c0..c3 during the pass

// Wet-surface modulation — host uploads CWeather::WetRoads-driven
// values once per scene. Reads in default_pp_PS at c63.
static float wetnessParams[4] = { 0.0f, 0.5f, 3.0f, 2.0f };
void
setWetness(float wetness, float diffuseDarken, float specBoost, float powerMul)
{
	wetnessParams[0] = wetness;
	wetnessParams[1] = diffuseDarken;
	wetnessParams[2] = specBoost;
	wetnessParams[3] = powerMul;
}

// Rain ripples — animated normal perturbation on wet up-facing
// surfaces. Host accumulates time only while raining + scales strength
// by CWeather::Rain. Reads in default_pp_PS at c65 (c64 is iblReflParams,
// c66+ stays free for future per-frame uniforms).
//   .x = accumulated rain time (seconds; drives animation phase)
//   .y = strength (0 = off, 1 = full)
//   .z = tile scale (world XY × this = ripple UV)
//   .w = reserved
static float rainRipplesParams[4] = { 0.0f, 0.0f, 0.5f, 0.0f };
void
setRainRipples(float time, float strength, float tileScale)
{
	rainRipplesParams[0] = time;
	rainRipplesParams[1] = strength;
	rainRipplesParams[2] = tileScale;
	rainRipplesParams[3] = 0.0f;
}

// Wet puddles — spatial variation on the existing wetMask. Reads in
// default_pp_PS at c66. Host fades strength with CWeather::Rain so
// puddles fill in during active rain and dry up as the weather clears.
//   .x = strength (0 = bypass)
//   .y = tile scale (~0.08 default; smaller = larger puddles)
//   .z = darken (reserved for future "deep puddle" tint)
//   .w = reserved
static float puddlesParams[4] = { 0.0f, 0.08f, 0.7f, 0.0f };
void
setPuddles(float strength, float tileScale)
{
	puddlesParams[0] = strength;
	puddlesParams[1] = tileScale;
	puddlesParams[2] = 0.7f;
	puddlesParams[3] = 0.0f;
}

// Underwater caustics — projected light cell pattern on submerged
// world surfaces. Reads in default_pp_PS at c67. Strength defaults to
// off; menu toggle on the host controls whether the effect runs.
//   .x = accumulated time (drives animation phase)
//   .y = strength
//   .z = water level (world Z below which caustics apply)
//   .w = reserved
static float causticsParams[4] = { 0.0f, 0.0f, 6.0f, 0.0f };
void
setCaustics(float time, float strength, float waterLevelZ)
{
	causticsParams[0] = time;
	causticsParams[1] = strength;
	causticsParams[2] = waterLevelZ;
	causticsParams[3] = 0.0f;
}

// Shoreline foam — beach-edge animated white-residue pattern.
// Reuses the caustics water level as the reference; the shader gates
// to a thin band above the waterline. Reads in default_pp_PS at c68.
//   .x = time (drives foam drift)
//   .y = strength
//   .z, .w = reserved
static float foamParams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
void
setFoam(float time, float strength)
{
	foamParams[0] = time;
	foamParams[1] = strength;
	foamParams[2] = 0.0f;
	foamParams[3] = 0.0f;
}

// Dynamic point lights — host (CDynamicLights) picks the top-N brightest
// CPointLights near the camera each frame and uploads them here. The
// receiver in default_pp_PS samples this array at c100 (count) + c101..
// c116 (8 lights × 2 vec4 each: position+radius, colour+intensity).
//
// This bypasses the librw lightingCB_Shader path (which only walks the
// World's directional light list + extra directionals added by certain
// entity SetupLighting overrides) and gives EVERY pp_PS-rendered atomic
// a uniform set of dynamic point lights. Buildings + props that don't
// invoke CPointLights::GenerateLightsAffectingObject now finally get
// illuminated by car headlights, lamps, gunshots, and explosions.
#define DYN_LIGHT_SLOTS 32
static float dynLightCount[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static float dynLightData[DYN_LIGHT_SLOTS * 2 * 4] = { 0 };
void
setDynamicPointLights(int count,
                      const float positions[][3],
                      const float radii[],
                      const float colours[][3],
                      const float intensities[])
{
	if(count < 0) count = 0;
	if(count > DYN_LIGHT_SLOTS) count = DYN_LIGHT_SLOTS;
	dynLightCount[0] = (float)count;
	// Zero unused slots so a residual previous-frame light doesn't bleed
	// in if the host shrinks the active count.
	for(int i = 0; i < DYN_LIGHT_SLOTS; i++){
		float *posReg = &dynLightData[(i*2 + 0) * 4];
		float *colReg = &dynLightData[(i*2 + 1) * 4];
		if(i < count){
			posReg[0] = positions[i][0];
			posReg[1] = positions[i][1];
			posReg[2] = positions[i][2];
			posReg[3] = radii[i];
			colReg[0] = colours[i][0];
			colReg[1] = colours[i][1];
			colReg[2] = colours[i][2];
			colReg[3] = intensities[i];
		}else{
			posReg[0] = posReg[1] = posReg[2] = 0.0f;
			posReg[3] = 0.0f;	// zero radius → if check skips it
			colReg[0] = colReg[1] = colReg[2] = 0.0f;
			colReg[3] = 0.0f;
		}
	}
}

void
uploadDynamicPointLights(void)
{
	if(!perPixelLightingEnabled)
		return;
	d3ddevice->SetPixelShaderConstantF(100, dynLightCount, 1);
	d3ddevice->SetPixelShaderConstantF(101, dynLightData, DYN_LIGHT_SLOTS * 2);
}

// IBL — the host (CGBuffer / CIBL) uploads four per-frame constants to
// PS slots c44..c47 (sky / horizon / ground / params). When iblEnabled
// is false uploadIBL() forces iblParams.x = 0 so the PS contribution
// multiplies out cleanly. This avoids needing a shader variant just to
// gate the gradient — saves on shader permutation bloat.
bool iblEnabled = false;
static float iblSkyColor    [4] = { 0.30f, 0.55f, 0.85f, 0 };
static float iblHorizonColor[4] = { 0.55f, 0.55f, 0.50f, 0 };
static float iblGroundColor [4] = { 0.10f, 0.09f, 0.07f, 0 };
static float iblParams      [4] = { 0.0f,  2.0f,  0.0f,  0 };	// .x=intensity (0 = off)

void
setIblColors(const float sky[3], const float horizon[3], const float ground[3], float intensity, float horizonExp)
{
	iblSkyColor[0] = sky[0]; iblSkyColor[1] = sky[1]; iblSkyColor[2] = sky[2];
	iblHorizonColor[0] = horizon[0]; iblHorizonColor[1] = horizon[1]; iblHorizonColor[2] = horizon[2];
	iblGroundColor[0] = ground[0]; iblGroundColor[1] = ground[1]; iblGroundColor[2] = ground[2];
	iblParams[0] = iblEnabled ? intensity : 0.0f;
	iblParams[1] = horizonExp;
}

// Phase 2 switch — when true, the PS samples the cube on s7 instead of
// the analytic gradient. Host (CIBL) is responsible for actually binding
// the cubemap to s7 before any pp draw fires.
void
setIblUseCube(bool useCube)
{
	iblParams[2] = useCube ? 1.0f : 0.0f;
}

void
uploadIBL(void)
{
	if(!perPixelLightingEnabled)
		return;
	float intensity = iblEnabled ? iblParams[0] : 0.0f;
	float live[4] = { intensity, iblParams[1], iblParams[2], iblParams[3] };
	d3ddevice->SetPixelShaderConstantF(44, iblSkyColor,    1);
	d3ddevice->SetPixelShaderConstantF(45, iblHorizonColor, 1);
	d3ddevice->SetPixelShaderConstantF(46, iblGroundColor, 1);
	d3ddevice->SetPixelShaderConstantF(47, live,           1);
	// Wet-surface params ride along with IBL — same per-frame cadence,
	// no separate dispatch needed.
	d3ddevice->SetPixelShaderConstantF(63, wetnessParams, 1);
	// Rain ripples — same cadence. Cheap one-vec4 upload; the shader
	// [branch]es on strength so dry scenes pay essentially nothing.
	d3ddevice->SetPixelShaderConstantF(65, rainRipplesParams, 1);
	// Puddles — same cadence.
	d3ddevice->SetPixelShaderConstantF(66, puddlesParams, 1);
	// Caustics — same cadence.
	d3ddevice->SetPixelShaderConstantF(67, causticsParams, 1);
	// Shoreline foam — same cadence.
	d3ddevice->SetPixelShaderConstantF(68, foamParams, 1);
}

// CSM receiver — uploads 3 cascade light-view-proj matrices + per-cascade
// split distances and tuning. Cascades 1..3 are uploaded contiguously
// (rather than 3 separate calls) for the matrix block. Strength gated to
// 0 by the host when CSM is disabled, so the receiver `lerp(1.0, raw, w)`
// short-circuits without any branch.
// Soft-PCF tuning — second c-reg block beyond the basic CSM upload.
// Host calls setCsmSoftness(mode, radiusMul) once per change; uploadCSM
// pushes it together with the rest of the cascade params so the receiver
// sees a consistent snapshot.
//   mode = 0 → Sharp (4-tap PCF)
//   mode = 1 → Soft  (16-tap Poisson)
//   mode = 2 → Ultra (32-tap Poisson) — best edge quality at ~2× the cost
//             of Soft. Recommended on modern hardware @ 1080p+.
//   mode = 3 → VSM   (Variance Shadow Maps via Chebyshev) — naturally
//             smooth edges, one .rg fetch per pixel, no Poisson kernel.
//             Trade-off: "light bleed" through thin/stacked occluders.
static float csmTuning2[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
void
setCsmSoftness(int mode, float radiusMul)
{
	csmTuning2[0] = (float)mode;
	csmTuning2[1] = (radiusMul > 0.1f ? radiusMul : 1.0f);
}

void
uploadCSM(const float matrices[48], const float splits[3], float strength,
          float invSize, float depthBias, float blendMetres)
{
	if(!perPixelLightingEnabled)
		return;
	// matrices is 3 × 16 floats, contiguous. PS register space: c48..c59.
	d3ddevice->SetPixelShaderConstantF(48, matrices,      12);
	float params[4] = { splits[0], splits[1], splits[2], strength };
	d3ddevice->SetPixelShaderConstantF(60, params, 1);
	float tuning[4] = { invSize, invSize, depthBias, blendMetres };
	d3ddevice->SetPixelShaderConstantF(61, tuning, 1);
	d3ddevice->SetPixelShaderConstantF(62, csmTuning2, 1);
}


void
createDefaultShaders(void)
{
	{
		static
#include "shaders/default_amb_VS.h"
		default_amb_VS = createVertexShader((void*)VS_NAME);
		assert(default_amb_VS);
	}
	{
		static
#include "shaders/default_amb_dir_VS.h"
		default_amb_dir_VS = createVertexShader((void*)VS_NAME);
		assert(default_amb_dir_VS);
	}
	{
		static
#include "shaders/default_all_VS.h"
		default_all_VS = createVertexShader((void*)VS_NAME);
		assert(default_all_VS);
	}

	{
		static
#include "shaders/default_PS.h"
		default_PS = createPixelShader((void*)PS_NAME);
		assert(default_PS);
	}
	{
		static
#include "shaders/default_tex_PS.h"
		default_tex_PS = createPixelShader((void*)PS_NAME);
		assert(default_tex_PS);
	}

	{
		static
#include "shaders/im2d_VS.h"
		im2d_VS = createVertexShader((void*)VS_NAME);
		assert(im2d_VS);
	}
	{
		static
#include "shaders/im2d_PS.h"
		im2d_PS = createPixelShader((void*)PS_NAME);
		assert(im2d_PS);
	}
	{
		static
#include "shaders/im2d_tex_PS.h"
		im2d_tex_PS = createPixelShader((void*)PS_NAME);
		assert(im2d_tex_PS);
	}

	// Per-pixel lighting variants. The pixel-shader uses ps_3_0 because the
	// 8-light array overflows ps_2_b's 32 constant slots.
	{
		static
#include "shaders/default_pp_amb_VS.h"
		default_pp_amb_VS = createVertexShader((void*)VS_NAME);
		assert(default_pp_amb_VS);
	}
	{
		static
#include "shaders/default_pp_amb_dir_VS.h"
		default_pp_amb_dir_VS = createVertexShader((void*)VS_NAME);
		assert(default_pp_amb_dir_VS);
	}
	{
		static
#include "shaders/default_pp_all_VS.h"
		default_pp_all_VS = createVertexShader((void*)VS_NAME);
		assert(default_pp_all_VS);
	}
	{
		static
#include "shaders/default_pp_PS.h"
		default_pp_PS = createPixelShader((void*)PS_NAME);
		assert(default_pp_PS);
	}
	{
		static
#include "shaders/default_pp_tex_PS.h"
		default_pp_tex_PS = createPixelShader((void*)PS_NAME);
		assert(default_pp_tex_PS);
	}

	// G-buffer variants. Same per-pixel lighting math as pp_*, plus MRT
	// output of packed world-normal + linear depth in COLOR1.
	{
		static
#include "shaders/default_pp_gbuf_amb_VS.h"
		default_pp_gbuf_amb_VS = createVertexShader((void*)VS_NAME);
		assert(default_pp_gbuf_amb_VS);
	}
	{
		static
#include "shaders/default_pp_gbuf_amb_dir_VS.h"
		default_pp_gbuf_amb_dir_VS = createVertexShader((void*)VS_NAME);
		assert(default_pp_gbuf_amb_dir_VS);
	}
	{
		static
#include "shaders/default_pp_gbuf_all_VS.h"
		default_pp_gbuf_all_VS = createVertexShader((void*)VS_NAME);
		assert(default_pp_gbuf_all_VS);
	}
	{
		static
#include "shaders/default_pp_gbuf_PS.h"
		default_pp_gbuf_PS = createPixelShader((void*)PS_NAME);
		assert(default_pp_gbuf_PS);
	}
	{
		static
#include "shaders/default_pp_gbuf_tex_PS.h"
		default_pp_gbuf_tex_PS = createPixelShader((void*)PS_NAME);
		assert(default_pp_gbuf_tex_PS);
	}
}

void
destroyDefaultShaders(void)
{
	destroyVertexShader(default_amb_VS);
	default_amb_VS = nil;
	destroyVertexShader(default_amb_dir_VS);
	default_amb_dir_VS = nil;
	destroyVertexShader(default_all_VS);
	default_all_VS = nil;

	destroyPixelShader(default_PS);
	default_PS = nil;
	destroyPixelShader(default_tex_PS);
	default_tex_PS = nil;

	destroyVertexShader(im2d_VS);
	im2d_VS = nil;
	destroyPixelShader(im2d_PS);
	im2d_PS = nil;
	destroyPixelShader(im2d_tex_PS);
	im2d_tex_PS = nil;

	if(default_pp_amb_VS){ destroyVertexShader(default_pp_amb_VS); default_pp_amb_VS = nil; }
	if(default_pp_amb_dir_VS){ destroyVertexShader(default_pp_amb_dir_VS); default_pp_amb_dir_VS = nil; }
	if(default_pp_all_VS){ destroyVertexShader(default_pp_all_VS); default_pp_all_VS = nil; }
	if(default_pp_PS){ destroyPixelShader(default_pp_PS); default_pp_PS = nil; }
	if(default_pp_tex_PS){ destroyPixelShader(default_pp_tex_PS); default_pp_tex_PS = nil; }

	if(default_pp_gbuf_amb_VS){ destroyVertexShader(default_pp_gbuf_amb_VS); default_pp_gbuf_amb_VS = nil; }
	if(default_pp_gbuf_amb_dir_VS){ destroyVertexShader(default_pp_gbuf_amb_dir_VS); default_pp_gbuf_amb_dir_VS = nil; }
	if(default_pp_gbuf_all_VS){ destroyVertexShader(default_pp_gbuf_all_VS); default_pp_gbuf_all_VS = nil; }
	if(default_pp_gbuf_PS){ destroyPixelShader(default_pp_gbuf_PS); default_pp_gbuf_PS = nil; }
	if(default_pp_gbuf_tex_PS){ destroyPixelShader(default_pp_gbuf_tex_PS); default_pp_gbuf_tex_PS = nil; }
}


void
lightingCB_Fix(Atomic *atomic)
{
	WorldLights lightData;
	Light *directionals[8];
	Light *locals[8];
	lightData.directionals = directionals;
	lightData.numDirectionals = 8;
	lightData.locals = locals;
	lightData.numLocals = 8;

	((World*)engine->currentWorld)->enumerateLights(atomic, &lightData);

	int i, n;
	RGBA amb;
	D3DLIGHT9 light;
	light.Type = D3DLIGHT_DIRECTIONAL;
	//light.Diffuse =  { 0.8f, 0.8f, 0.8f, 1.0f };
	light.Specular = { 0.0f, 0.0f, 0.0f, 0.0f };
	light.Ambient =  { 0.0f, 0.0f, 0.0f, 0.0f };
	light.Position = { 0.0f, 0.0f, 0.0f };
	//light.Direction = { 0.0f, 0.0f, -1.0f };
	light.Range = 0.0f;
	light.Falloff = 0.0f;
	light.Attenuation0 = 0.0f;
	light.Attenuation1 = 0.0f;
	light.Attenuation2 = 0.0f;
	light.Theta = 0.0f;
	light.Phi = 0.0f;

	convColor(&amb, &lightData.ambient);
	d3d::setRenderState(D3DRS_AMBIENT, D3DCOLOR_RGBA(amb.red, amb.green, amb.blue, amb.alpha));

	n = 0;
	for(i = 0; i < lightData.numDirectionals; i++){
		if(n >= MAX_LIGHTS)
			return;
		Light *l = lightData.directionals[i];
		light.Type = D3DLIGHT_DIRECTIONAL;
		light.Diffuse =  *(D3DCOLORVALUE*)&l->color;
		light.Direction = *(D3DVECTOR*)&l->getFrame()->getLTM()->at;
		d3ddevice->SetLight(n, &light);
		d3ddevice->LightEnable(n, TRUE);
		n++;
	}

	for(i = 0; i < lightData.numLocals; i++){
		if(n >= MAX_LIGHTS)
			return;
		Light *l = lightData.locals[i];
		switch(l->getType()){
		case Light::POINT:
			light.Type = D3DLIGHT_POINT;
			light.Diffuse =  *(D3DCOLORVALUE*)&l->color;
			light.Position = *(D3DVECTOR*)&l->getFrame()->getLTM()->pos;
			light.Direction.x = 0.0f;
			light.Direction.y = 0.0f;
			light.Direction.z = 0.0f;
			light.Range = l->radius;
			light.Falloff = 1.0f;
			light.Attenuation0 = 1.0f;
			light.Attenuation1 = 0.0f/l->radius;
			light.Attenuation2 = 5.0f/(l->radius*l->radius);
			d3ddevice->SetLight(n, &light);
			d3ddevice->LightEnable(n, TRUE);
			n++;
			break;

		case Light::SPOT:
			light.Type = D3DLIGHT_SPOT;
			light.Diffuse =  *(D3DCOLORVALUE*)&l->color;
			light.Position = *(D3DVECTOR*)&l->getFrame()->getLTM()->pos;
			light.Direction = *(D3DVECTOR*)&l->getFrame()->getLTM()->at;
			light.Range = l->radius;
			light.Falloff = 1.0f;
			light.Attenuation0 = 1.0f;
			light.Attenuation1 = 0.0f/l->radius;
			light.Attenuation2 = 5.0f/(l->radius*l->radius);
			light.Theta = l->getAngle()*2.0f;
			light.Phi = light.Theta;
			d3ddevice->SetLight(n, &light);
			d3ddevice->LightEnable(n, TRUE);
			n++;
			break;

		case Light::SOFTSPOT:
			light.Type = D3DLIGHT_SPOT;
			light.Diffuse =  *(D3DCOLORVALUE*)&l->color;
			light.Position = *(D3DVECTOR*)&l->getFrame()->getLTM()->pos;
			light.Direction = *(D3DVECTOR*)&l->getFrame()->getLTM()->at;
			light.Range = l->radius;
			light.Falloff = 1.0f;
			light.Attenuation0 = 1.0f;
			light.Attenuation1 = 0.0f/l->radius;
			light.Attenuation2 = 5.0f/(l->radius*l->radius);
			light.Theta = 0.0f;
			light.Phi = l->getAngle()*2.0f;
			d3ddevice->SetLight(n, &light);
			d3ddevice->LightEnable(n, TRUE);
			n++;
			break;
		}
	}

	for(; n < MAX_LIGHTS; n++)
		d3ddevice->LightEnable(n, FALSE);
}


struct LightVS
{
	V3d color; float param0;
	V3d position; float param1;
	V3d direction; float param2;
};

void
setAmbient(const RGBAf &color)
{
	if(!equal(d3dShaderState.ambient, color)){
		d3dShaderState.ambient = color;
		d3ddevice->SetVertexShaderConstantF(VSLOC_ambLight, (float*)&color, 1);
	}
	// PS register space is separate from VS — re-upload every call so the
	// pp pixel shader never sees stale ambient (which would tint everything
	// toward whatever was in c15 from an earlier non-pp pass).
	if(perPixelLightingEnabled)
		d3ddevice->SetPixelShaderConstantF(VSLOC_ambLight, (float*)&color, 1);
}

void
setNumLights(int numDir, int numPoint, int numSpot)
{
	static int32 numLights[4*3];
	if(d3dShaderState.numDir != numDir ||
	   d3dShaderState.numPoint != numPoint ||
	   d3dShaderState.numSpot != numSpot){
		numLights[0] = d3dShaderState.numDir = numDir;
		numLights[4] = d3dShaderState.numPoint = numPoint;
		numLights[8] = d3dShaderState.numSpot = numSpot;
		d3ddevice->SetVertexShaderConstantI(VSLOC_numLights, numLights, 3);
	}
}

int32
uploadLights(WorldLights *lightData)
{
	int i;
	int bits = 0;
	float32 firstLight[4];
	firstLight[0] = 0;	// directional
	firstLight[1] = 0;	// point
	firstLight[2] = 0;	// spot
	firstLight[3] = 0;

	if(lightData->numAmbients)
		bits |= VSLIGHT_AMBIENT;

	LightVS directionals[8];
	LightVS points[8];
	LightVS spots[8];
	for(i = 0; i < lightData->numDirectionals; i++){
		Light *l = lightData->directionals[i];
		directionals[i].color.x = l->color.red;
		directionals[i].color.y = l->color.green;
		directionals[i].color.z = l->color.blue;
		directionals[i].direction = l->getFrame()->getLTM()->at;
		bits |= VSLIGHT_DIRECT;
	}

	int np = 0;
	int ns = 0;
	for(i = 0; i < lightData->numLocals; i++){
		Light *l = lightData->locals[i];

		switch(l->getType()){
		case Light::POINT:
			points[np].color.x = l->color.red;
			points[np].color.y = l->color.green;
			points[np].color.z = l->color.blue;
			points[np].param0 = l->radius;
			points[np].position = l->getFrame()->getLTM()->pos;
			np++;
			bits |= VSLIGHT_POINT;
			break;
		case Light::SPOT:
		case Light::SOFTSPOT:
			spots[ns].color.x = l->color.red;
			spots[ns].color.y = l->color.green;
			spots[ns].color.z = l->color.blue;
			spots[ns].param0 = l->radius;
			spots[ns].position = l->getFrame()->getLTM()->pos;
			spots[ns].direction = l->getFrame()->getLTM()->at;
			spots[ns].param1 = l->minusCosAngle;
			// lower bound of falloff
			if(l->getType() == Light::SOFTSPOT)
				spots[ns].param2 = 0.0f;
			else
				spots[ns].param2 = 1.0f;
			bits |= VSLIGHT_SPOT;
			ns++;
			break;
		}
	}

	firstLight[0] = 0;
	int numDir = lightData->numDirectionals;
	firstLight[1] = numDir + firstLight[0];
	int numPoint = np;
	firstLight[2] = numPoint + firstLight[1];
	int numSpot = ns;

	setNumLights(numDir, numPoint, numSpot);
	if(d3dShaderState.lightOffset[0] != firstLight[0] ||
	   d3dShaderState.lightOffset[1] != firstLight[1] ||
	   d3dShaderState.lightOffset[2] != firstLight[2]){
		d3dShaderState.lightOffset[0] = firstLight[0];
		d3dShaderState.lightOffset[1] = firstLight[1];
		d3dShaderState.lightOffset[2] = firstLight[2];
		d3ddevice->SetVertexShaderConstantF(VSLOC_lightOffset, firstLight, 1);
	}

	int32 off = VSLOC_lights;
	if(numDir)
		d3ddevice->SetVertexShaderConstantF(off, (float*)&directionals, numDir*3);
	off += numDir*3;

	if(numPoint)
		d3ddevice->SetVertexShaderConstantF(off, (float*)&points, numPoint*3);
	off += numPoint*3;

	if(numSpot)
		d3ddevice->SetVertexShaderConstantF(off, (float*)&spots, numSpot*3);

	if(perPixelLightingEnabled){
		// Mirror the lighting constants to pixel-shader registers. The
		// pp pixel shader uses c41 for the int-as-float counts (PS has no
		// integer constant bank in 2_x/3_0) and c16/c17 for the rest.
		float numLightsPS[4] = {
			(float)numDir, (float)numPoint, (float)numSpot, 0.0f
		};
		d3ddevice->SetPixelShaderConstantF(PSLOC_numLights, numLightsPS, 1);
		d3ddevice->SetPixelShaderConstantF(VSLOC_lightOffset, firstLight, 1);

		int32 poff = VSLOC_lights;
		if(numDir)
			d3ddevice->SetPixelShaderConstantF(poff, (float*)&directionals, numDir*3);
		poff += numDir*3;

		if(numPoint)
			d3ddevice->SetPixelShaderConstantF(poff, (float*)&points, numPoint*3);
		poff += numPoint*3;

		if(numSpot)
			d3ddevice->SetPixelShaderConstantF(poff, (float*)&spots, numSpot*3);
	}

	return bits;
}

int32
lightingCB_Shader(Atomic *atomic)
{
	WorldLights lightData;
	Light *directionals[8];
	Light *locals[8];
	lightData.directionals = directionals;
	lightData.numDirectionals = 8;
	lightData.locals = locals;
	lightData.numLocals = 8;

	if(atomic->geometry->flags & rw::Geometry::LIGHT){
		((World*)engine->currentWorld)->enumerateLights(atomic, &lightData);
		setAmbient(lightData.ambient);
		return uploadLights(&lightData);
	}else{
		static const RGBAf black = { 0.0f, 0.0f, 0.0f, 0.0f };
		setAmbient(black);
		setNumLights(0, 0, 0);
		return 0;
	}
}

int32
lightingCB_Shader(void)
{
	WorldLights lightData;
	Light *directionals[8];
	Light *locals[8];
	lightData.directionals = directionals;
	lightData.numDirectionals = 8;
	lightData.locals = locals;
	lightData.numLocals = 8;

	((World*)engine->currentWorld)->enumerateLights(&lightData);
	setAmbient(lightData.ambient);
	return uploadLights(&lightData);
}

static RawMatrix identityXform = {
	{ 1.0f, 0.0f, 0.0f }, 0.0f,
	{ 0.0f, 1.0f, 0.0f }, 0.0f,
	{ 0.0f, 0.0f, 1.0f }, 0.0f,
	{ 0.0f, 0.0f, 0.0f }, 1.0f
};

void
uploadMatrices(void)
{
	RawMatrix combined;
	Camera *cam = engine->currentCamera;
	d3ddevice->SetVertexShaderConstantF(VSLOC_world, (float*)&identityXform, 4);
	d3ddevice->SetVertexShaderConstantF(VSLOC_normal, (float*)&identityXform, 4);

	RawMatrix::mult(&combined, &cam->devView, &cam->devProj);
	d3ddevice->SetVertexShaderConstantF(VSLOC_combined, (float*)&combined, 4);
}

void
uploadMatrices(Matrix *worldMat)
{
	RawMatrix combined, world, worldview;
	Camera *cam = engine->currentCamera;
	convMatrix(&world, worldMat);
	d3ddevice->SetVertexShaderConstantF(VSLOC_world, (float*)&world, 4);
	// TODO: inverse transpose
	d3ddevice->SetVertexShaderConstantF(VSLOC_normal, (float*)&world, 4);

	RawMatrix::mult(&worldview, &world, &cam->devView);
	RawMatrix::mult(&combined, &worldview, &cam->devProj);
	d3ddevice->SetVertexShaderConstantF(VSLOC_combined, (float*)&combined, 4);

	if(perPixelLightingEnabled){
		// Push world-space camera position + specular power to the pp PS
		// (slot c42). The default specular power of 32 gives a moderately
		// tight highlight; surfaces with surfProps.specular drive its
		// intensity inside the shader.
		V3d eye = cam->getFrame()->getLTM()->pos;
		float eyeArr[4] = { eye.x, eye.y, eye.z, 32.0f };
		d3ddevice->SetPixelShaderConstantF(42, eyeArr, 1);
	}

	if(gbufferEnabled){
		// viewParams.x = 1/farClip — used by VS to normalise view-space depth
		// before passing it down to the G-buffer pixel shader.
		float farClip = cam->farPlane > 0.0001f ? cam->farPlane : 250.0f;
		float viewParams[4] = { 1.0f/farClip, cam->nearPlane, cam->farPlane, 0.0f };
		d3ddevice->SetVertexShaderConstantF(11, viewParams, 1);
	}
}



#endif

}
}
