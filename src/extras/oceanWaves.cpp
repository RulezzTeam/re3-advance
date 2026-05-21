#define WITHD3D
#include "common.h"

#ifdef POSTFX_HDR

#include "main.h"
#include "Camera.h"
#include "Timer.h"
#include "oceanWaves.h"

RwRaster *COceanWaves::pHeightField = nil;
bool COceanWaves::Enabled = false;
float COceanWaves::WindSpeed = 6.0f;
float COceanWaves::WindDirX = 0.707f;	// 45° default
float COceanWaves::WindDirY = 0.707f;
float COceanWaves::Amplitude = 0.35f;
float COceanWaves::Choppiness = 0.6f;
float COceanWaves::FoamThreshold = 0.7f;
float COceanWaves::TileSize = 64.0f;

#ifdef RW_D3D9
static void *oceanWaves_PS = nil;
static rw::Camera *oceanCam = nil;
static RwIm2DVertex sOceanVtx[4];
static RwImVertexIndex sOceanIdx[6] = { 0, 1, 2, 0, 2, 3 };
#endif

void
COceanWaves::InitOnce(void)
{
	// Static fields are zero-initialised; no extra setup needed at
	// engine startup. Open() lazily allocates the RT + shader on first
	// invocation so non-postfx-HDR configs pay nothing.
}

void
COceanWaves::Open(RwCamera *cam)
{
	(void)cam;
	if(pHeightField != nil) return;

#ifdef RW_D3D9
	if(oceanWaves_PS == nil){
#include "shaders/obj/oceanWaves_PS.inc"
		oceanWaves_PS = rw::d3d::createPixelShader(oceanWaves_PS_cso);
	}

	// 256² RGBA16F heightfield. F16 has ample range for the ±5m wave
	// amplitudes a real ocean produces, and tagging RGBA16F keeps the
	// foam mask in .a free for the future water consumer.
	int32 fmt = (int32)rw::Raster::CAMERATEXTURE | (int32)rw::Raster::F16_RGBA;
	pHeightField = RwRasterCreate(256, 256, 0, fmt);
	if(pHeightField == nil) return;

	// Mini im2d quad for the heightfield draw. We borrow the bloom-
	// camera pattern from CPostFX (CreateBloomCam) but inline it here
	// so the dependency stays local. The quad covers full UV 0..1
	// over the 256² target.
	rw::Camera *rwCam = rw::Camera::create();
	rwCam->frameBuffer = (rw::Raster*)pHeightField;
	rwCam->zBuffer = nil;
	rw::V2d vw = { 1.0f, 1.0f };
	rwCam->setViewWindow(&vw);
	rwCam->setNearPlane(0.5f);
	rwCam->setFarPlane(2.0f);
	oceanCam = rwCam;

	// Quad vertices — full target.
	float w = 256.0f, h = 256.0f;
	for(int i = 0; i < 4; i++){
		float x = (i == 1 || i == 2) ? w : 0.0f;
		float y = (i == 2 || i == 3) ? h : 0.0f;
		float u = (i == 1 || i == 2) ? 1.0f : 0.0f;
		float v = (i == 2 || i == 3) ? 1.0f : 0.0f;
		RwIm2DVertexSetScreenX(&sOceanVtx[i], x);
		RwIm2DVertexSetScreenY(&sOceanVtx[i], y);
		RwIm2DVertexSetScreenZ(&sOceanVtx[i], RwIm2DGetNearScreenZ());
		RwIm2DVertexSetCameraZ(&sOceanVtx[i], 0.5f);
		RwIm2DVertexSetRecipCameraZ(&sOceanVtx[i], 2.0f);
		RwIm2DVertexSetU(&sOceanVtx[i], u, 2.0f);
		RwIm2DVertexSetV(&sOceanVtx[i], v, 2.0f);
		RwIm2DVertexSetIntRGBA(&sOceanVtx[i], 255, 255, 255, 255);
	}
#else
	(void)cam;
#endif
}

void
COceanWaves::Close(void)
{
#ifdef RW_D3D9
	if(oceanCam != nil){
		oceanCam->frameBuffer = nil;
		oceanCam->destroy();
		oceanCam = nil;
	}
	if(pHeightField != nil){
		RwRasterDestroy(pHeightField);
		pHeightField = nil;
	}
	if(oceanWaves_PS != nil){
		rw::d3d::destroyPixelShader(oceanWaves_PS);
		oceanWaves_PS = nil;
	}
#endif
}

void
COceanWaves::Render(RwCamera *cam)
{
	if(!Enabled) return;
#ifdef RW_D3D9
	// Lazy-open on first Render so configs that never enable waves
	// pay zero startup cost.
	if(pHeightField == nil) Open(cam);
	if(pHeightField == nil || oceanWaves_PS == nil || oceanCam == nil) return;

	static float sOceanTime = 0.0f;
	sOceanTime += CTimer::GetTimeStepNonClipped() * (1.0f/50.0f);

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

	RwCameraEndUpdate(cam);
	RwCameraBeginUpdate((RwCamera*)oceanCam);

	// c10 = time, wind dir, wind speed
	float p1[4] = { sOceanTime, WindDirX, WindDirY, WindSpeed };
	rw::d3d::d3ddevice->SetPixelShaderConstantF(10, p1, 1);

	// c11 = amplitude, choppiness, foam threshold, tile size
	float p2[4] = { Amplitude, Choppiness, FoamThreshold, TileSize };
	rw::d3d::d3ddevice->SetPixelShaderConstantF(11, p2, 1);

	rw::d3d::im2dOverridePS = oceanWaves_PS;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, sOceanVtx, 4, sOceanIdx, 6);
	rw::d3d::im2dOverridePS = nil;

	RwCameraEndUpdate((RwCamera*)oceanCam);
	RwCameraBeginUpdate(cam);
#else
	(void)cam;
#endif
}

#endif
