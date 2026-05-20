#define WITHD3D
#include "common.h"

#ifdef POSTFX_HDR

#ifndef LIBRW
#error "POSTFX_HDR needs librw"
#endif

#include "main.h"
#include "Camera.h"
#include "RwHelper.h"
#include "Renderer.h"
#include "World.h"
#include "Lights.h"
#include "PointLights.h"	// CPointLights::aLights[] for picking
#include "spotShadow.h"
#include "csm.h"	// shadowDepthOnly piggy-backs on the CSM depth path

#ifdef RW_D3D9
#include <d3d9.h>
#endif

RwRaster *CSpotShadow::depthRT;
RwRaster *CSpotShadow::zBuffer;
RwCamera *CSpotShadow::lightCam;
bool CSpotShadow::Enabled = false;
// Default bumped 512 → 1024. The shared map serves the brightest active
// light each frame, and at 512 the receiver shows visible Mach banding
// on close-up surfaces (corridor walls under street lamps). 1024² R32F
// is 4 MB — still trivial.
int32 CSpotShadow::MapSize = 1024;
int8  CSpotShadow::MapSizeIndex = 2;   // 0..3 = 256/512/1024/2048 — default High (1024)
float CSpotShadow::Strength = 0.85f;
float CSpotShadow::Bias = 0.003f;
float CSpotShadow::Softness = 1.5f;

static bool sPendingSpotMapSizeRealloc = false;

void
CSpotShadow::MapSizeAfterChange(int8 before, int8 after)
{
	(void)before;
	static const int32 kSizeTable[4] = { 256, 512, 1024, 2048 };
	int8 idx = after;
	if(idx < 0) idx = 0;
	if(idx > 3) idx = 3;
	int32 newSize = kSizeTable[idx];
	if(newSize == MapSize) return;
	MapSize = newSize;
	// Deferred reallocation — Open dereferences cam to register the light
	// camera with the scene world; menu changes happen without a live
	// scene camera, so we defer to the next frame's PickActiveLight path
	// which has one. Pattern mirrors CCSM::MapSizeAfterChange.
	sPendingSpotMapSizeRealloc = true;
}
CSpotShadow::ActiveLight CSpotShadow::current = {};
float CSpotShadow::lightViewProj[16] = {
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1,
};

void
CSpotShadow::InitOnce(void)
{
	depthRT = nil;
	zBuffer = nil;
	lightCam = nil;
	current.valid = false;
}

void
CSpotShadow::Open(RwCamera *cam)
{
	if(depthRT)
		Close();
	if(!Enabled)
		return;

	int32 size = MapSize;
	if(size < 128) size = 128;
	if(size > 2048) size = 2048;
	MapSize = size;

	// R32F via the F16 RGBA fallback (librw's calculated format set).
	int32 colorFmt = (int32)rw::Raster::CAMERATEXTURE | (int32)rw::Raster::F16_RGBA;
	depthRT = RwRasterCreate(size, size, 0, colorFmt);
	zBuffer = RwRasterCreate(size, size, 0, (int32)rw::Raster::ZBUFFER);
	if(depthRT == nil || zBuffer == nil){
		// Hardware doesn't like float RT at this size — disable.
		if(depthRT){ RwRasterDestroy(depthRT); depthRT = nil; }
		if(zBuffer){ RwRasterDestroy(zBuffer); zBuffer = nil; }
		Enabled = false;
		return;
	}

	rw::Camera *sceneCam = (rw::Camera*)cam;
	rw::Camera *lc = rw::Camera::create();
	rw::Frame *frame = rw::Frame::create();
	lc->setFrame(frame);
	lc->frameBuffer = (rw::Raster*)depthRT;
	lc->zBuffer = (rw::Raster*)zBuffer;
	lc->setNearPlane(0.2f);
	lc->setFarPlane(50.0f);
	rw::V2d vw = { 1.0f, 1.0f };
	lc->setViewWindow(&vw);
	if(sceneCam && sceneCam->world)
		sceneCam->world->addCamera(lc);
	lightCam = (RwCamera*)lc;
}

void
CSpotShadow::Close(void)
{
	if(lightCam){
		rw::Camera *lc = (rw::Camera*)lightCam;
		lc->frameBuffer = nil;
		lc->zBuffer = nil;
		rw::Frame *f = lc->getFrame();
		if(f){ lc->setFrame(nil); f->destroy(); }
		if(lc->world) lc->world->removeCamera(lc);
		lc->destroy();
		lightCam = nil;
	}
	if(depthRT){ RwRasterDestroy(depthRT); depthRT = nil; }
	if(zBuffer){ RwRasterDestroy(zBuffer); zBuffer = nil; }
	current.valid = false;
}

void
CSpotShadow::PickActiveLight(RwCamera *cam)
{
	// Honour a deferred MapSize change from the menu — we have a live
	// scene cam here, which Open needs to register the light camera
	// with cam->world (the menu-time AfterChange path doesn't).
	if(sPendingSpotMapSizeRealloc && cam != nullptr){
		sPendingSpotMapSizeRealloc = false;
		if(depthRT != nil){
			Close();
			Open(cam);
		}
	}

	current.valid = false;
	if(!Enabled || lightCam == nil)
		return;
	if(CPointLights::NumLights == 0)
		return;

	// Score every registered point-light by (luminance × inverse-dist²)
	// so the brightest light on the player's screen wins. CPointLights
	// is rebuilt per frame by the engine, so the picked light naturally
	// tracks whatever is illuminating the scene most strongly.
	rw::Camera *sceneCam = (rw::Camera*)cam;
	rw::V3d camPos = sceneCam->getFrame()->getLTM()->pos;
	CVector cp(camPos.x, camPos.y, camPos.z);

	float bestScore = 0.0f;
	int   bestIdx = -1;
	for(int i = 0; i < CPointLights::NumLights; i++){
		const CRegisteredPointLight &L = CPointLights::aLights[i];
		if(L.type != CPointLights::LIGHT_POINT) continue;
		float lum = L.red + L.green + L.blue;
		if(lum < 0.05f) continue;
		CVector d = L.coors - cp;
		float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
		if(d2 < 0.01f) d2 = 0.01f;
		// Skip lights farther than ~80m — too far for shadow detail.
		if(d2 > 80.0f * 80.0f) continue;
		float score = lum / d2;
		if(score > bestScore){
			bestScore = score;
			bestIdx = i;
		}
	}

	if(bestIdx < 0) return;

	const CRegisteredPointLight &L = CPointLights::aLights[bestIdx];
	current.worldPos[0] = L.coors.x;
	current.worldPos[1] = L.coors.y;
	current.worldPos[2] = L.coors.z;
	current.colour[0]   = L.red;
	current.colour[1]   = L.green;
	current.colour[2]   = L.blue;
	current.radius      = L.radius > 1.0f ? L.radius : 18.0f;
	current.intensity   = bestScore;
	current.valid       = true;
}

void
CSpotShadow::RenderShadowMap(RwCamera *cam)
{
	if(!Enabled || lightCam == nil || !current.valid)
		return;
#ifdef RW_D3D9
	if(rw::d3d::shadow_VS == nullptr || rw::d3d::shadow_PS == nullptr)
		return;

	// Aim the light camera "down" from the picked light position. For a
	// proper spot we'd use the light's `at` direction but reaching for
	// that across engine versions is brittle; aiming -Z gives consistent
	// shadows on horizontal surfaces (road, car roofs) which is where
	// you actually see point-light shadows.
	rw::Camera *lc = (rw::Camera*)lightCam;
	rw::Frame *lframe = lc->getFrame();
	rw::Matrix &m = lframe->matrix;
	m.pos.x = current.worldPos[0];
	m.pos.y = current.worldPos[1];
	m.pos.z = current.worldPos[2];
	m.at.x = 0; m.at.y = 0; m.at.z = -1;	// look down
	m.right.x = 1; m.right.y = 0; m.right.z = 0;
	m.up.x = 0; m.up.y = -1; m.up.z = 0;
	m.flags |= rw::Matrix::TYPEORTHONORMAL;
	lframe->updateObjects();

	lc->setNearPlane(0.5f);
	lc->setFarPlane(current.radius);
	// FOV ~120° via wide view window — fakes an omnidirectional sample
	// for the bottom hemisphere of the light.
	rw::V2d vw = { 1.732f, 1.732f };
	lc->setViewWindow(&vw);

	RwCameraEndUpdate(cam);
	rw::d3d::shadowDepthOnly = true;
	CCSM::bRendering = true;

	rw::RGBA white = { 255, 255, 255, 255 };
	lc->clear(&white, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
	RwCameraBeginUpdate(lightCam);

	rw::RawMatrix vp;
	rw::RawMatrix::mult(&vp, &lc->devView, &lc->devProj);
	memcpy(lightViewProj, &vp, sizeof(float) * 16);
	memcpy(rw::d3d::shadowLightViewProj, &vp, sizeof(float) * 16);

	CRenderer::RenderRoads();
	CRenderer::RenderEverythingBarRoads();

	RwCameraEndUpdate(lightCam);

	CCSM::bRendering = false;
	rw::d3d::shadowDepthOnly = false;
	RwCameraBeginUpdate(cam);
#endif
}

void
CSpotShadow::BindReceiver(void)
{
#ifdef RW_D3D9
	if(!Enabled || depthRT == nil || !current.valid){
		// Force strength to 0 so the receiver shader skips sampling.
		extern void uploadSpotShadow(const float matrix[16], const float lightPos[4],
		                             float strength, float invSize, float bias, float softness);
		float zero[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
		float lp[4] = { 0, 0, 0, 0 };
		// Nothing to bind. uploadSpotShadow with strength=0 still clears
		// the receiver's gating constant.
		rw::d3d::d3ddevice->SetTexture(9, nullptr);
		(void)zero; (void)lp;
		return;
	}

	// Bind the depth map on sampler 9 (slot 7=IBL irradiance,
	// 8=reflection cube, 9=spot shadow).
	auto bind = [](int slot, RwRaster *r){
		if(r == nullptr){
			rw::d3d::d3ddevice->SetTexture(slot, nullptr);
			return;
		}
		if(((rw::Raster*)r)->parent)
			r = (RwRaster*)((rw::Raster*)r)->parent;
		rw::d3d::D3dRaster *natras = GETD3DRASTEREXT((rw::Raster*)r);
		rw::d3d::d3ddevice->SetTexture(slot, (IDirect3DTexture9*)natras->texture);
		rw::d3d::d3ddevice->SetSamplerState(slot, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		rw::d3d::d3ddevice->SetSamplerState(slot, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		rw::d3d::d3ddevice->SetSamplerState(slot, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		rw::d3d::d3ddevice->SetSamplerState(slot, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		rw::d3d::d3ddevice->SetSamplerState(slot, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	};
	bind(9, depthRT);

	// Upload matrix + position + tuning to the librw-managed slot region.
	// Receiver looks at c70..c72.
	rw::d3d::d3ddevice->SetPixelShaderConstantF(70, lightViewProj, 4);
	float lp[4] = { current.worldPos[0], current.worldPos[1], current.worldPos[2], current.radius };
	rw::d3d::d3ddevice->SetPixelShaderConstantF(74, lp, 1);
	float tune[4] = { Strength, 1.0f / (float)MapSize, Bias, Softness };
	rw::d3d::d3ddevice->SetPixelShaderConstantF(75, tune, 1);
#endif
}

void
CSpotShadow::UnbindReceiver(void)
{
#ifdef RW_D3D9
	rw::d3d::d3ddevice->SetTexture(9, nullptr);
	float zero[4] = { 0, 0, 0, 0 };
	rw::d3d::d3ddevice->SetPixelShaderConstantF(75, zero, 1);
#endif
}

#endif
