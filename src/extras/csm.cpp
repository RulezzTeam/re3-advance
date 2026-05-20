#define WITHD3D
#include "common.h"

#ifdef POSTFX_CSM

#ifndef LIBRW
#error "POSTFX_CSM needs librw"
#endif

#include "main.h"
#include "Camera.h"
#include "Timecycle.h"
#include "RwHelper.h"
#include "Renderer.h"
#include "csm.h"


CCSM::Cascade CCSM::Cascades[CSM_NUM_CASCADES];
bool CCSM::Enabled = false;
bool CCSM::bRendering = false;
int32 CCSM::MapSize = CSM_DEFAULT_SIZE;
int8  CCSM::MapSizeIndex = 1;	// 0=1024,1=2048,2=4096; default High (2048)
float CCSM::Strength = 0.85f;
float CCSM::Bias = 0.003f;
int32 CCSM::NumCascades = 3;
int32 CCSM::SoftnessMode = 0;	// 0 = Sharp (4-tap), 1 = Soft (16-tap), 2 = Ultra (32-tap), 3 = VSM
float CCSM::SoftnessRadius = 1.5f;	// slight softening by default

// Set by MapSizeAfterChange so the next ComputeCascades sees the
// requested size and triggers Close()+Open() with the live scene camera
// (which we have at that point but don't have at menu-change time). The
// menu callback only marks the request — actual reallocation runs on a
// known-good engine state.
static bool sPendingMapSizeRealloc = false;

void
CCSM::MapSizeAfterChange(int8 before, int8 after)
{
	(void)before;
	static const int32 kSizeTable[3] = { 1024, 2048, 4096 };
	int8 idx = after;
	if(idx < 0) idx = 0;
	if(idx > 2) idx = 2;
	int32 newSize = kSizeTable[idx];
	if(newSize == MapSize) return;
	MapSize = newSize;
	// Defer the reallocation — we don't have the scene camera here, and
	// CCSM::Open dereferences sceneCam->world to register the cascade
	// cameras. ComputeCascades runs each frame with a valid camera and
	// can do the Close+Open then.
	sPendingMapSizeRealloc = true;
}

void *csmDepthVS;
void *csmDepthPS;
void *csmSkinDepthVS;	// bone-aware variant for peds + drivers

// Helper to bind a CAMERATEXTURE raster on a sampler slot for the
// receiver pass. Same pattern as postfx.cpp's BindRasterToSampler;
// duplicated here to keep CCSM self-contained.
static void
BindCascadeSampler(int slot, RwRaster *raster)
{
#ifdef RW_D3D9
	if(raster == nil){
		rw::d3d::d3ddevice->SetTexture(slot, nil);
		return;
	}
	if(((rw::Raster*)raster)->parent)
		raster = (RwRaster*)((rw::Raster*)raster)->parent;
	rw::d3d::D3dRaster *natras = GETD3DRASTEREXT((rw::Raster*)raster);
	rw::d3d::d3ddevice->SetTexture(slot, (IDirect3DTexture9*)natras->texture);
	rw::d3d::d3ddevice->SetSamplerState(slot, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	rw::d3d::d3ddevice->SetSamplerState(slot, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	rw::d3d::d3ddevice->SetSamplerState(slot, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	rw::d3d::d3ddevice->SetSamplerState(slot, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	rw::d3d::d3ddevice->SetSamplerState(slot, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
#endif
}

// Practical Split Scheme split distances (mix uniform + log) for the view
// frustum's z range. lambda = 0.5 favours mid-range detail.
static void
ComputeSplitDistances(float zNear, float zFar, float outSplits[CSM_NUM_CASCADES + 1])
{
	const float lambda = 0.5f;
	outSplits[0] = zNear;
	for(int i = 1; i <= CCSM::NumCascades; i++){
		float si = (float)i / (float)CCSM::NumCascades;
		float logSplit = zNear * powf(zFar / zNear, si);
		float linSplit = zNear + (zFar - zNear) * si;
		outSplits[i] = lambda * logSplit + (1.0f - lambda) * linSplit;
	}
}

void
CCSM::InitOnce(void)
{
	for(int i = 0; i < CSM_NUM_CASCADES; i++){
		Cascades[i].depthRT = nil;
		Cascades[i].zBuffer = nil;
		Cascades[i].lightCam = nil;
		Cascades[i].splitDist = 0;
	}
}

void
CCSM::Open(RwCamera *cam)
{
	if(Cascades[0].depthRT)
		Close();
	if(!Enabled)
		return;

#ifdef RW_D3D9
	// Lazy-load the depth shaders on first Open. Stay loaded across
	// scene transitions — there's no per-scene state in them.
	if(csmDepthVS == nullptr){
		#include "shaders/obj/csm_depth_VS.inc"
		csmDepthVS = rw::d3d::createVertexShader(csm_depth_VS_cso);
	}
	if(csmDepthPS == nullptr){
		#include "shaders/obj/csm_depth_PS.inc"
		csmDepthPS = rw::d3d::createPixelShader(csm_depth_PS_cso);
	}
	if(csmSkinDepthVS == nullptr){
		#include "shaders/obj/csm_skin_depth_VS.inc"
		csmSkinDepthVS = rw::d3d::createVertexShader(csm_skin_depth_VS_cso);
	}
	// Expose them to librw so the modified default + skin render
	// callbacks can swap to depth-only emission during the cascade pass.
	// csm_skin_depth_VS applies bone matrices to Position so peds /
	// drivers cast correctly-deformed shadows instead of T-poses.
	rw::d3d::shadow_VS = csmDepthVS;
	rw::d3d::shadow_PS = csmDepthPS;
	rw::d3d::shadow_skin_VS = csmSkinDepthVS;
#endif

	int32 size = MapSize;
	if(size < 512) size = 512;
	else if(size > 4096) size = 4096;
	MapSize = size;

	rw::Camera *sceneCam = (rw::Camera*)cam;

	// Use the rw "f16" path is overkill for depth; R32F would be ideal but
	// we don't have it in the librw format table yet. F16_RGBA is the
	// closest float colour format and gives us 16-bit precision per channel
	// which is enough for ~mm precision on a 250m far clip cascade.
	int32 colorFmt = (int32)rw::Raster::CAMERATEXTURE | (int32)rw::Raster::F16_RGBA;

	for(int i = 0; i < CSM_NUM_CASCADES; i++){
		Cascades[i].depthRT = RwRasterCreate(size, size, 0, colorFmt);
		Cascades[i].zBuffer = RwRasterCreate(size, size, 0, (int32)rw::Raster::ZBUFFER);

		rw::Camera *lc = rw::Camera::create();
		rw::Frame *frame = rw::Frame::create();
		lc->setFrame(frame);
		lc->frameBuffer = (rw::Raster*)Cascades[i].depthRT;
		lc->zBuffer = (rw::Raster*)Cascades[i].zBuffer;
		lc->setNearPlane(0.5f);
		lc->setFarPlane(500.0f);
		rw::V2d vw = { 1.0f, 1.0f };	// orthographic; we set proj manually each frame
		lc->setViewWindow(&vw);
		if(sceneCam->world)
			sceneCam->world->addCamera(lc);
		Cascades[i].lightCam = (RwCamera*)lc;
	}
}

void
CCSM::Close(void)
{
	for(int i = 0; i < CSM_NUM_CASCADES; i++){
		if(Cascades[i].lightCam){
			rw::Camera *lc = (rw::Camera*)Cascades[i].lightCam;
			lc->frameBuffer = nil;
			lc->zBuffer = nil;
			rw::Frame *f = lc->getFrame();
			if(f){
				lc->setFrame(nil);
				f->destroy();
			}
			if(lc->world)
				lc->world->removeCamera(lc);
			lc->destroy();
			Cascades[i].lightCam = nil;
		}
		if(Cascades[i].depthRT){ RwRasterDestroy(Cascades[i].depthRT); Cascades[i].depthRT = nil; }
		if(Cascades[i].zBuffer){ RwRasterDestroy(Cascades[i].zBuffer); Cascades[i].zBuffer = nil; }
	}
}

void
CCSM::ComputeCascades(RwCamera *cam)
{
	// Honour a deferred Close+Open from MapSizeAfterChange now that we
	// have a valid scene camera. Open dereferences cam->world to register
	// the cascade cameras, which we couldn't do at menu-change time.
	if(sPendingMapSizeRealloc && cam != nullptr){
		sPendingMapSizeRealloc = false;
		if(Cascades[0].depthRT != nil){
			Close();
			Open(cam);
		}
	}

	if(!Enabled || Cascades[0].lightCam == nil)
		return;

	rw::Camera *sceneCam = (rw::Camera*)cam;
	float zNear = sceneCam->nearPlane;
	float zFar  = sceneCam->farPlane;

	float splits[CSM_NUM_CASCADES + 1];
	ComputeSplitDistances(zNear, zFar, splits);

	// Sun direction in world space. CTimeCycle returns the direction *to*
	// the sun; we want the light propagation direction (away from sun).
	CVector sunTo = CTimeCycle::GetSunDirection();
	rw::V3d lightDir = { -sunTo.x, -sunTo.y, -sunTo.z };
	float len = sqrtf(lightDir.x*lightDir.x + lightDir.y*lightDir.y + lightDir.z*lightDir.z);
	if(len > 0.0001f){
		lightDir.x /= len; lightDir.y /= len; lightDir.z /= len;
	}else{
		lightDir.x = 0; lightDir.y = 0; lightDir.z = -1;
	}

	// World-up; if the sun is straight down (rare), use X axis as up to
	// avoid a degenerate cross product.
	rw::V3d worldUp = { 0, 0, 1 };
	if(fabsf(lightDir.z) > 0.95f){
		worldUp.x = 1; worldUp.y = 0; worldUp.z = 0;
	}

	rw::V3d camPos = sceneCam->getFrame()->getLTM()->pos;
	rw::V3d camFwd = sceneCam->getFrame()->getLTM()->at;

	for(int i = 0; i < NumCascades; i++){
		float sNear = splits[i];
		float sFar  = splits[i + 1];
		float midDist = (sNear + sFar) * 0.5f;
		float radius = (sFar - sNear) * 0.5f;
		// Bounding sphere of cascade slice — radius equals half the
		// frustum slice's diagonal but for simplicity we approximate as
		// half the slice depth. Good enough for first-pass CSM.

		// Sphere centre = camera + camFwd * midDist
		rw::V3d centre;
		centre.x = camPos.x + camFwd.x * midDist;
		centre.y = camPos.y + camFwd.y * midDist;
		centre.z = camPos.z + camFwd.z * midDist;

		// Texel-snap centre so the shadow map stays stable as the camera
		// rotates / moves. Snap unit = 2 * radius / size in world units
		// per texel.
		float texelSize = (2.0f * radius) / (float)MapSize;
		centre.x = floorf(centre.x / texelSize) * texelSize;
		centre.y = floorf(centre.y / texelSize) * texelSize;
		centre.z = floorf(centre.z / texelSize) * texelSize;

		// Build light view matrix: position = centre - lightDir * pullBack,
		// orientation = lookAt(centre - lightDir, centre, worldUp).
		float pullBack = 250.0f;	// extrude back so casters behind frustum still occlude
		rw::Camera *lc = (rw::Camera*)Cascades[i].lightCam;
		rw::Frame *lframe = lc->getFrame();
		rw::Matrix &m = lframe->matrix;
		m.pos.x = centre.x - lightDir.x * pullBack;
		m.pos.y = centre.y - lightDir.y * pullBack;
		m.pos.z = centre.z - lightDir.z * pullBack;
		m.at = lightDir;
		// Right = normalize(cross(worldUp, at))
		rw::V3d right;
		right.x = worldUp.y * m.at.z - worldUp.z * m.at.y;
		right.y = worldUp.z * m.at.x - worldUp.x * m.at.z;
		right.z = worldUp.x * m.at.y - worldUp.y * m.at.x;
		float rl = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
		if(rl > 0.0001f){ right.x/=rl; right.y/=rl; right.z/=rl; }
		m.right = right;
		// Up = cross(at, right)
		m.up.x = m.at.y * m.right.z - m.at.z * m.right.y;
		m.up.y = m.at.z * m.right.x - m.at.x * m.right.z;
		m.up.z = m.at.x * m.right.y - m.at.y * m.right.x;
		m.flags |= rw::Matrix::TYPEORTHONORMAL;
		lframe->updateObjects();

		lc->setNearPlane(0.5f);
		lc->setFarPlane(pullBack + radius + 50.0f);
		// Orthographic-ish via tiny near plane; rw::Camera supports a
		// PROJECTION setting we'd ideally pull on — left as TODO since
		// librw exposes its setProjection API for us already.
		rw::V2d vw = { radius, radius };
		lc->setViewWindow(&vw);

		Cascades[i].splitDist = sFar;
	}
}

// Build the world-to-cascade-clip matrix for each cascade. Called right
// after rw::Camera::beginUpdate fires on the light camera, since librw
// fills devView (world→view) and devProj (view→clip) at that point. We
// store the product row-major for the receiver-side shader (matches the
// `mul(pos, mat)` HLSL convention used in default_PS.hlsl).
static void
CacheCascadeMatrices(void)
{
	for(int i = 0; i < CCSM::NumCascades; i++){
		rw::Camera *lc = (rw::Camera*)CCSM::Cascades[i].lightCam;
		if(lc == nullptr) continue;
		rw::RawMatrix vp;
		rw::RawMatrix::mult(&vp, &lc->devView, &lc->devProj);
		// Store row-major; the receiver uses `mul(pos, mat)` so the
		// translation must end up in the last row, which RawMatrix::mult
		// already gives us.
		memcpy(CCSM::Cascades[i].lightViewProj, &vp, sizeof(float) * 16);
	}
}

void
CCSM::RenderShadowMaps(RwCamera *cam)
{
	if(!Enabled || Cascades[0].lightCam == nil)
		return;
	if(bRendering)
		return;
#ifdef RW_D3D9
	if(csmDepthVS == nullptr || csmDepthPS == nullptr)
		return;
#endif

	ComputeCascades(cam);

	RwCameraEndUpdate(cam);
	bRendering = true;
#ifdef RW_D3D9
	rw::d3d::shadowDepthOnly = true;
#endif

	rw::RGBA white;
	white.red = 255; white.green = 255; white.blue = 255; white.alpha = 255;

	for(int i = 0; i < NumCascades; i++){
		rw::Camera *lc = (rw::Camera*)Cascades[i].lightCam;
		lc->clear(&white, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
		RwCameraBeginUpdate(Cascades[i].lightCam);

#ifdef RW_D3D9
		// devView/devProj are valid only after beginUpdate. Cache the
		// world→cascade-clip matrix here so the receiver gets the same
		// transform we used to rasterise depths into the cascade RT.
		{
			rw::RawMatrix vp;
			rw::RawMatrix::mult(&vp, &lc->devView, &lc->devProj);
			memcpy(Cascades[i].lightViewProj, &vp, sizeof(float) * 16);
			// Push to librw's shadow_VS lightViewProj slot so every
			// atomic draw uses the right matrix.
			memcpy(rw::d3d::shadowLightViewProj, &vp, sizeof(float) * 16);
		}
#endif

		// Walk the visible scene and emit depths through the modified
		// default + skin render callbacks. CRenderer keeps a list of
		// atomics that passed the scene-camera visibility test; we
		// reuse it under the light camera. Casters outside the scene
		// frustum but inside the cascade ortho won't appear in their
		// shadow — pancaking via the cascade's 250m back-extrusion in
		// ComputeCascades partially compensates.
		CRenderer::RenderRoads();
		CRenderer::RenderEverythingBarRoads();

		RwCameraEndUpdate(Cascades[i].lightCam);
	}

#ifdef RW_D3D9
	rw::d3d::shadowDepthOnly = false;
#endif
	bRendering = false;
	RwCameraBeginUpdate(cam);
}

void
CCSM::BindReceiver(void)
{
	if(!Enabled || Cascades[0].depthRT == nil)
		return;

#ifdef RW_D3D9
	// Bind the 3 cascade depth maps on samplers s4..s6 + upload matrices,
	// split distances, and tuning to PS c48..c61.
	BindCascadeSampler(4, Cascades[0].depthRT);
	BindCascadeSampler(5, Cascades[1].depthRT);
	BindCascadeSampler(6, Cascades[2].depthRT);

	float matrices[48];
	for(int i = 0; i < 3; i++){
		memcpy(matrices + i * 16, Cascades[i].lightViewProj, sizeof(float) * 16);
	}
	float splits[3] = {
		Cascades[0].splitDist,
		Cascades[1].splitDist,
		Cascades[2].splitDist,
	};
	rw::d3d::setCsmSoftness((int)SoftnessMode, SoftnessRadius);
	rw::d3d::uploadCSM(matrices, splits, Strength, 1.0f / (float)MapSize, Bias, 4.0f /* blend metres */);
#endif
}

void
CCSM::UnbindReceiver(void)
{
#ifdef RW_D3D9
	BindCascadeSampler(4, nil);
	BindCascadeSampler(5, nil);
	BindCascadeSampler(6, nil);
	// Force receiver strength to 0 so the next scene render (without
	// cascades) doesn't sample stale textures.
	float matrices[48] = { 0 };
	float splits[3] = { 0, 0, 0 };
	rw::d3d::uploadCSM(matrices, splits, 0.0f, 1.0f / (float)MapSize, Bias, 0.0f);
#endif
}

#endif
