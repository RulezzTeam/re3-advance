#define WITHD3D
#include "common.h"

#ifdef EXTENDED_PIPELINES

#include "main.h"
#include "RwHelper.h"
#include "Lights.h"
#include "Timecycle.h"
#include "FileMgr.h"
#include "Clock.h"
#include "Weather.h"
#include "TxdStore.h"
#include "Renderer.h"
#include "World.h"
#include "Camera.h"
#include "custompipes.h"

#ifndef LIBRW
#error "Need librw for EXTENDED_PIPELINES"
#endif

namespace CustomPipes {

rw::int32 CustomMatOffset;

void*
CustomMatCtor(void *object, int32, int32)
{
	CustomMatExt *ext = GetCustomMatExt((rw::Material*)object);
	ext->glossTex = nil;
	ext->haveGloss = false;
	return object;
}

void*
CustomMatCopy(void *dst, void *src, int32, int32)
{
	CustomMatExt *srcext = GetCustomMatExt((rw::Material*)src);
	CustomMatExt *dstext = GetCustomMatExt((rw::Material*)dst);
	dstext->glossTex = srcext->glossTex;
	dstext->haveGloss = srcext->haveGloss;
	return dst;
}



rw::TexDictionary *neoTxd;

bool bRenderingEnvMap;
// Menu-tweakable resolution preference. The actual EnvMapSize is sampled
// from this at CustomPipeInit() (next game launch after a change).
int32 EnvMapSizePref = 1024;
int8  EnvMapSizeIndex = 2;	// 0=256, 1=512, 2=1024, 3=2048
int32 EnvMapSize = 1024;	// 8x sharper than the original 128 default
#ifdef MULTI_ENVMAP
// Round-robin slots. Slot 0 follows the player; the others sample different
// forward distances so neighbouring vehicles get more representative reflections.
rw::Camera *EnvMapCams[NUM_ENVMAPS];
rw::Texture *EnvMapTexs[NUM_ENVMAPS];
rw::Camera *EnvMapCam;            // alias for EnvMapCams[0] — keeps old call sites working
rw::Texture *EnvMapTex;           // alias for EnvMapTexs[0]
static int  envMapCurrentSlot;    // which slot to update this frame
// 16 sample-point distances with a near-quadratic spacing — denser sampling
// close to the player (where most vehicles are visible) and sparser at
// distance (where reflection accuracy matters less). Slot 0 stays on the
// player.
static const float envMapForwardOffsets[NUM_ENVMAPS] = {
	  0.0f,   8.0f,  18.0f,  30.0f,
	 44.0f,  60.0f,  78.0f,  98.0f,
	120.0f, 144.0f, 170.0f, 200.0f,
	232.0f, 268.0f, 308.0f, 350.0f
};
#else
rw::Camera *EnvMapCam;
rw::Texture *EnvMapTex;
#endif
rw::Texture *EnvMaskTex;
static rw::RWDEVICE::Im2DVertex EnvScreenQuad[4];
static int16 QuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

static rw::Camera*
CreateEnvMapCam(rw::World *world)
{
	rw::Raster *fbuf = rw::Raster::create(EnvMapSize, EnvMapSize, 0, rw::Raster::CAMERATEXTURE);
	if(fbuf){
		rw::Raster *zbuf = rw::Raster::create(EnvMapSize, EnvMapSize, 0, rw::Raster::ZBUFFER);
		if(zbuf){
			rw::Frame *frame = rw::Frame::create();
			if(frame){
				rw::Camera *cam = rw::Camera::create();
				if(cam){
					cam->frameBuffer = fbuf;
					cam->zBuffer = zbuf;
					cam->setFrame(frame);
					cam->setNearPlane(0.1f);
					cam->setFarPlane(250.0f);
					rw::V2d vw = { 2.0f, 2.0f };
					cam->setViewWindow(&vw);
					world->addCamera(cam);
					EnvMapTex = rw::Texture::create(fbuf);
					EnvMapTex->setFilter(rw::Texture::LINEAR);

					frame->matrix.right.x = -1.0f;
					frame->matrix.up.y = -1.0f;
					frame->matrix.update();
					return cam;
				}
				frame->destroy();
			}
			zbuf->destroy();
		}
		fbuf->destroy();
	}
	return nil;
}

static void
DestroyCam(rw::Camera *cam)
{
	if(cam == nil)
		return;
	if(cam->frameBuffer){
		cam->frameBuffer->destroy();
		cam->frameBuffer = nil;
	}
	if(cam->zBuffer){
		cam->zBuffer->destroy();
		cam->zBuffer = nil;
	}
	rw::Frame *f = cam->getFrame();
	if(f){
		cam->setFrame(nil);
		f->destroy();
	}
	cam->world->removeCamera(cam);
	cam->destroy();
}

void
RenderEnvMapScene(void)
{
	CRenderer::RenderRoads();
	CRenderer::RenderEverythingBarRoads();
	CRenderer::RenderFadingInEntities();
}

#ifdef MULTI_ENVMAP
static void
RenderOneEnvMap(rw::Camera *cam, const rw::V3d &samplePos)
{
	cam->getFrame()->matrix.pos = samplePos;
	cam->getFrame()->transform(&cam->getFrame()->matrix, rw::COMBINEREPLACE);

	rw::RGBA skycol;
	skycol.red = CTimeCycle::GetSkyBottomRed();
	skycol.green = CTimeCycle::GetSkyBottomGreen();
	skycol.blue = CTimeCycle::GetSkyBottomBlue();
	skycol.alpha = 255;
	cam->clear(&skycol, rwCAMERACLEARZ|rwCAMERACLEARIMAGE);
	RwCameraBeginUpdate(cam);
	bRenderingEnvMap = true;
	RenderEnvMapScene();
	bRenderingEnvMap = false;

	if(EnvMaskTex){
		rw::SetRenderState(rw::VERTEXALPHA, TRUE);
		rw::SetRenderState(rw::SRCBLEND, rw::BLENDZERO);
		rw::SetRenderState(rw::DESTBLEND, rw::BLENDSRCCOLOR);
		rw::SetRenderStatePtr(rw::TEXTURERASTER, EnvMaskTex->raster);
		rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, EnvScreenQuad, 4, QuadIndices, 6);
		rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
		rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
	}
	RwCameraEndUpdate(cam);
}
#endif

void
EnvMapRender(void)
{
	if(VehiclePipeSwitch != VEHICLEPIPE_NEO)
		return;

	RwCameraEndUpdate(Scene.camera);

#ifdef MULTI_ENVMAP
	// Update one slot per frame round-robin to spread cost. Each slot is
	// centred at a different forward offset from the camera so vehicles at
	// varying distances get plausible reflections.
	int slot = envMapCurrentSlot;
	envMapCurrentSlot = (envMapCurrentSlot + 1) % NUM_ENVMAPS;

	rw::V3d camPos = FindPlayerCoors();
	rw::V3d camAt = TheCamera.GetForward();	// world-space forward vector
	rw::V3d samplePos;
	samplePos.x = camPos.x + camAt.x * envMapForwardOffsets[slot];
	samplePos.y = camPos.y + camAt.y * envMapForwardOffsets[slot];
	samplePos.z = camPos.z + camAt.z * envMapForwardOffsets[slot];
	if(EnvMapCams[slot])
		RenderOneEnvMap(EnvMapCams[slot], samplePos);
#else
	// Neo does this differently, but i'm not quite convinced it's much better
	rw::V3d camPos = FindPlayerCoors();
	EnvMapCam->getFrame()->matrix.pos = camPos;
	EnvMapCam->getFrame()->transform(&EnvMapCam->getFrame()->matrix, rw::COMBINEREPLACE);

	rw::RGBA skycol;
	skycol.red = CTimeCycle::GetSkyBottomRed();
	skycol.green = CTimeCycle::GetSkyBottomGreen();
	skycol.blue = CTimeCycle::GetSkyBottomBlue();
	skycol.alpha = 255;
	EnvMapCam->clear(&skycol, rwCAMERACLEARZ|rwCAMERACLEARIMAGE);
	RwCameraBeginUpdate(EnvMapCam);
	bRenderingEnvMap = true;
	RenderEnvMapScene();
	bRenderingEnvMap = false;

	if(EnvMaskTex){
		rw::SetRenderState(rw::VERTEXALPHA, TRUE);
		rw::SetRenderState(rw::SRCBLEND, rw::BLENDZERO);
		rw::SetRenderState(rw::DESTBLEND, rw::BLENDSRCCOLOR);
		rw::SetRenderStatePtr(rw::TEXTURERASTER, EnvMaskTex->raster);
		rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, EnvScreenQuad, 4, QuadIndices, 6);
		rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
		rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
	}
	RwCameraEndUpdate(EnvMapCam);
#endif


	RwCameraBeginUpdate(Scene.camera);

	// debug env map
//	rw::SetRenderStatePtr(rw::TEXTURERASTER, EnvMapTex->raster);
//	rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, EnvScreenQuad, 4, QuadIndices, 6);
}

#ifdef MULTI_ENVMAP
// Pick the slot whose sample point is closest to the vehicle. Works for any
// NUM_ENVMAPS by walking the offset table.
int
EnvMapSlotFor(const rw::V3d &worldPos)
{
	rw::V3d camPos = TheCamera.GetPosition();
	rw::V3d delta;
	delta.x = worldPos.x - camPos.x;
	delta.y = worldPos.y - camPos.y;
	delta.z = worldPos.z - camPos.z;
	float dist = sqrtf(delta.x*delta.x + delta.y*delta.y + delta.z*delta.z);
	int best = 0;
	float bestDelta = 1e30f;
	for(int i = 0; i < NUM_ENVMAPS; i++){
		float d = dist - envMapForwardOffsets[i];
		if(d < 0) d = -d;
		if(d < bestDelta){
			bestDelta = d;
			best = i;
		}
	}
	return best;
}
#endif

static void
EnvMapInit(void)
{
	if(neoTxd)
		EnvMaskTex = neoTxd->find("CarReflectionMask");

#ifdef MULTI_ENVMAP
	for(int i = 0; i < NUM_ENVMAPS; i++){
		EnvMapCams[i] = CreateEnvMapCam(Scene.world);
		EnvMapTexs[i] = EnvMapTex;	// CreateEnvMapCam wrote into the global
		EnvMapTex = nil;
	}
	EnvMapCam = EnvMapCams[0];	// alias for legacy callers
	EnvMapTex = EnvMapTexs[0];
#else
	EnvMapCam = CreateEnvMapCam(Scene.world);
#endif

	int width = EnvMapCam->frameBuffer->width;
	int height = EnvMapCam->frameBuffer->height;
	float screenZ = RwIm2DGetNearScreenZ();
	float recipZ = 1.0f/EnvMapCam->nearPlane;

	EnvScreenQuad[0].setScreenX(0.0f);
	EnvScreenQuad[0].setScreenY(0.0f);
	EnvScreenQuad[0].setScreenZ(screenZ);
	EnvScreenQuad[0].setCameraZ(EnvMapCam->nearPlane);
	EnvScreenQuad[0].setRecipCameraZ(recipZ);
	EnvScreenQuad[0].setColor(255, 255, 255, 255);
	EnvScreenQuad[0].setU(0.0f, recipZ);
	EnvScreenQuad[0].setV(0.0f, recipZ);

	EnvScreenQuad[1].setScreenX(0.0f);
	EnvScreenQuad[1].setScreenY(height);
	EnvScreenQuad[1].setScreenZ(screenZ);
	EnvScreenQuad[1].setCameraZ(EnvMapCam->nearPlane);
	EnvScreenQuad[1].setRecipCameraZ(recipZ);
	EnvScreenQuad[1].setColor(255, 255, 255, 255);
	EnvScreenQuad[1].setU(0.0f, recipZ);
	EnvScreenQuad[1].setV(1.0f, recipZ);

	EnvScreenQuad[2].setScreenX(width);
	EnvScreenQuad[2].setScreenY(height);
	EnvScreenQuad[2].setScreenZ(screenZ);
	EnvScreenQuad[2].setCameraZ(EnvMapCam->nearPlane);
	EnvScreenQuad[2].setRecipCameraZ(recipZ);
	EnvScreenQuad[2].setColor(255, 255, 255, 255);
	EnvScreenQuad[2].setU(1.0f, recipZ);
	EnvScreenQuad[2].setV(1.0f, recipZ);

	EnvScreenQuad[3].setScreenX(width);
	EnvScreenQuad[3].setScreenY(0.0f);
	EnvScreenQuad[3].setScreenZ(screenZ);
	EnvScreenQuad[3].setCameraZ(EnvMapCam->nearPlane);
	EnvScreenQuad[3].setRecipCameraZ(recipZ);
	EnvScreenQuad[3].setColor(255, 255, 255, 255);
	EnvScreenQuad[3].setU(1.0f, recipZ);
	EnvScreenQuad[3].setV(0.0f, recipZ);
}

#ifdef MULTI_ENVMAP
static void EnvMapShutdown(void);
static void EnvMapInit(void);

void
RebuildEnvMaps(void)
{
	if(EnvMapCams[0] == nil)
		return;	// not initialised yet — first CustomPipeInit will pick up the new size
	int32 size = EnvMapSizePref;
	if(size <= 256) size = 256;
	else if(size <= 512) size = 512;
	else if(size <= 1024) size = 1024;
	else size = 2048;
	if(size == EnvMapSize)
		return;
	EnvMapShutdown();
	EnvMapSize = size;
	EnvMapInit();
}

void
EnvMapSizeAfterChange(int8 before, int8 after)
{
	static const int32 kSizeTable[4] = { 256, 512, 1024, 2048 };
	int8 idx = after;
	if(idx < 0) idx = 0;
	if(idx > 3) idx = 3;
	EnvMapSizePref = kSizeTable[idx];
	RebuildEnvMaps();
}
#endif

static void
EnvMapShutdown(void)
{
#ifdef MULTI_ENVMAP
	for(int i = 0; i < NUM_ENVMAPS; i++){
		if(EnvMapTexs[i]){
			EnvMapTexs[i]->raster = nil;
			EnvMapTexs[i]->destroy();
			EnvMapTexs[i] = nil;
		}
		if(EnvMapCams[i]){
			DestroyCam(EnvMapCams[i]);
			EnvMapCams[i] = nil;
		}
	}
	EnvMapCam = nil;
	EnvMapTex = nil;
#else
	EnvMapTex->raster = nil;
	EnvMapTex->destroy();
	EnvMapTex = nil;
	DestroyCam(EnvMapCam);
	EnvMapCam = nil;
#endif
}

/*
 * Tweak values
 */

#define INTERP_SETUP \
		int h1 = CClock::GetHours();								  \
		int h2 = (h1+1)%24;										  \
		int w1 = CWeather::OldWeatherType;								  \
		int w2 = CWeather::NewWeatherType;								  \
		float timeInterp = (CClock::GetSeconds()/60.0f + CClock::GetMinutes())/60.0f;	  \
		float c0 = (1.0f-timeInterp)*(1.0f-CWeather::InterpolationValue);				  \
		float c1 = timeInterp*(1.0f-CWeather::InterpolationValue);					  \
		float c2 = (1.0f-timeInterp)*CWeather::InterpolationValue;					  \
		float c3 = timeInterp*CWeather::InterpolationValue;
#define INTERP(v) v[h1][w1]*c0 + v[h2][w1]*c1 + v[h1][w2]*c2 + v[h2][w2]*c3;
#define INTERPF(v,f) v[h1][w1].f*c0 + v[h2][w1].f*c1 + v[h1][w2].f*c2 + v[h2][w2].f*c3;

InterpolatedFloat::InterpolatedFloat(float init)
{
	curInterpolator = 61;	// compared against second
	for(int h = 0; h < 24; h++)
		for(int w = 0; w < NUMWEATHERS; w++)
			data[h][w] = init;
}

void
InterpolatedFloat::Read(char *s, int line, int field)
{
	sscanf(s, "%f", &data[line][field]);
}

float
InterpolatedFloat::Get(void)
{
	if(curInterpolator != CClock::GetSeconds()){
		INTERP_SETUP
		curVal = INTERP(data);
		curInterpolator = CClock::GetSeconds();
	}
	return curVal;
}

InterpolatedColor::InterpolatedColor(const Color &init)
{
	curInterpolator = 61;	// compared against second
	for(int h = 0; h < 24; h++)
		for(int w = 0; w < NUMWEATHERS; w++)
			data[h][w] = init;
}

void
InterpolatedColor::Read(char *s, int line, int field)
{
	int r, g, b, a;
	sscanf(s, "%i, %i, %i, %i", &r, &g, &b, &a);
	data[line][field] = Color(r/255.0f, g/255.0f, b/255.0f, a/255.0f);
}

Color
InterpolatedColor::Get(void)
{
	if(curInterpolator != CClock::GetSeconds()){
		INTERP_SETUP
		curVal.r = INTERPF(data, r);
		curVal.g = INTERPF(data, g);
		curVal.b = INTERPF(data, b);
		curVal.a = INTERPF(data, a);
		curInterpolator = CClock::GetSeconds();
	}
	return curVal;
}

void
InterpolatedLight::Read(char *s, int line, int field)
{
	int r, g, b, a;
	sscanf(s, "%i, %i, %i, %i", &r, &g, &b, &a);
	data[line][field] = Color(r/255.0f, g/255.0f, b/255.0f, a/100.0f);
}

char*
ReadTweakValueTable(char *fp, InterpolatedValue &interp)
{
	char buf[24], *p;
	int c;
	int line, field;

	line = 0;
	c = *fp++;
	while(c != '\0' && line < 24){
		field = 0;
		if(c != '\0' && c != '#'){
			while(c != '\0' && c != '\n' && field < NUMWEATHERS){
				p = buf;
				while(c != '\0' && c == '\t')
					c = *fp++;
				*p++ = c;
				while(c = *fp++, c != '\0' && c != '\t' && c != '\n')
					*p++ = c;
				*p++ = '\0';
				interp.Read(buf, line, field);
				field++;
			}
			line++;
		}
		while(c != '\0' && c != '\n')
			c = *fp++;
		c = *fp++;
	}
	return fp-1;
}



/*
 * Neo Vehicle pipe
 */

int32 VehiclePipeSwitch = VEHICLEPIPE_MATFX;
float VehicleShininess = 1.0f;
float VehicleSpecularity = 1.0f;
InterpolatedFloat Fresnel(0.4f);
InterpolatedFloat Power(18.0f);
InterpolatedLight DiffColor(Color(0.0f, 0.0f, 0.0f, 0.0f));
InterpolatedLight SpecColor(Color(0.7f, 0.7f, 0.7f, 1.0f));
rw::ObjPipeline *vehiclePipe;

void
AttachVehiclePipe(rw::Atomic *atomic)
{
	atomic->pipeline = vehiclePipe;
}

void
AttachVehiclePipe(rw::Clump *clump)
{
	FORLIST(lnk, clump->atomics)
		AttachVehiclePipe(rw::Atomic::fromClump(lnk));
}



/*
 * Neo World pipe
 */

bool LightmapEnable;
float LightmapMult = 1.0f;
InterpolatedFloat WorldLightmapBlend(1.0f);
rw::ObjPipeline *worldPipe;

void
AttachWorldPipe(rw::Atomic *atomic)
{
	atomic->pipeline = worldPipe;
}

void
AttachWorldPipe(rw::Clump *clump)
{
	FORLIST(lnk, clump->atomics)
		AttachWorldPipe(rw::Atomic::fromClump(lnk));
}




/*
 * Neo Gloss pipe
 */

bool GlossEnable;
float GlossMult = 1.0f;
rw::ObjPipeline *glossPipe;

rw::Texture*
GetGlossTex(rw::Material *mat)
{
	if(neoTxd == nil)
		return nil;
	CustomMatExt *ext = GetCustomMatExt(mat);
	if(!ext->haveGloss){
		char glossname[128];
		strcpy(glossname, mat->texture->name);
		strcat(glossname, "_gloss");
		ext->glossTex = neoTxd->find(glossname);
		ext->haveGloss = true;
	}
	return ext->glossTex;
}

void
AttachGlossPipe(rw::Atomic *atomic)
{
	atomic->pipeline = glossPipe;
}

void
AttachGlossPipe(rw::Clump *clump)
{
	FORLIST(lnk, clump->atomics)
		AttachWorldPipe(rw::Atomic::fromClump(lnk));
}



/*
 * Neo Rim pipes
 */

bool RimlightEnable;
float RimlightMult = 1.0f;
InterpolatedColor RampStart(Color(0.0f, 0.0f, 0.0f, 1.0f));
InterpolatedColor RampEnd(Color(1.0f, 1.0f, 1.0f, 1.0f));
InterpolatedFloat Offset(0.5f);
InterpolatedFloat Scale(1.5f);
InterpolatedFloat Scaling(2.0f);
rw::ObjPipeline *rimPipe;
rw::ObjPipeline *rimSkinPipe;

void
AttachRimPipe(rw::Atomic *atomic)
{
	if(rw::Skin::get(atomic->geometry))
		atomic->pipeline = rimSkinPipe;
	else
		atomic->pipeline = rimPipe;
}

void
AttachRimPipe(rw::Clump *clump)
{
	FORLIST(lnk, clump->atomics)
		AttachRimPipe(rw::Atomic::fromClump(lnk));
}

/*
 * High level stuff
 */

void
CustomPipeInit(void)
{
#if defined(PER_PIXEL_LIGHTING) && defined(RW_D3D9)
	// Switch librw's default/skin render path to the per-pixel lighting
	// variants. This must happen before any geometry is drawn.
	rw::d3d::perPixelLightingEnabled = true;
#endif
#ifdef MULTI_ENVMAP
	// Snapshot the user-selected resolution at init time. Valid values are
	// 256, 512, 1024, 2048; anything else gets clamped to the next valid
	// step.
	int32 size = EnvMapSizePref;
	if(size < 256) size = 256;
	else if(size < 512) size = 256;
	else if(size < 1024) size = 512;
	else if(size < 2048) size = 1024;
	else size = 2048;
	EnvMapSize = size;
#endif
	RwStream *stream = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, "neo/neo.txd");
	if(stream == nil)
		printf("Error: couldn't open 'neo/neo.txd'\n");
	else{
		if(RwStreamFindChunk(stream, rwID_TEXDICTIONARY, nil, nil))
			neoTxd = RwTexDictionaryGtaStreamRead(stream);
		RwStreamClose(stream, nil);
	}

	EnvMapInit();

	CreateVehiclePipe();
	CreateWorldPipe();
	CreateGlossPipe();
	CreateRimLightPipes();
}

void
CustomPipeShutdown(void)
{
	DestroyVehiclePipe();
	DestroyWorldPipe();
	DestroyGlossPipe();
	DestroyRimLightPipes();

	EnvMapShutdown();

	if(neoTxd){
		neoTxd->destroy();
		neoTxd = nil;
	}
}

void
CustomPipeRegister(void)
{
#ifdef RW_OPENGL
	CustomPipeRegisterGL();
#endif

	CustomMatOffset = rw::Material::registerPlugin(sizeof(CustomMatExt), MAKECHUNKID(rwVENDORID_ROCKSTAR, 0x80),
		CustomMatCtor, nil, CustomMatCopy);
}


// Load textures from generic as fallback

rw::TexDictionary *genericTxd;
rw::Texture *(*defaultFindCB)(const char *name);

static rw::Texture*
customFindCB(const char *name)
{
	rw::Texture *res = defaultFindCB(name);
	if(res == nil)
		res = genericTxd->find(name);
	return res;
}

void
SetTxdFindCallback(void)
{
	int slot = CTxdStore::FindTxdSlot("generic");
	CTxdStore::AddRef(slot);
	// TODO: function for this
	genericTxd = CTxdStore::GetSlot(slot)->texDict;
	assert(genericTxd);
	if(defaultFindCB == nil)
		defaultFindCB = rw::Texture::findCB;
	rw::Texture::findCB = customFindCB;
}

}

#endif
