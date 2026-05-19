#define WITHD3D
#include "common.h"

#ifdef POSTFX_HDR

#ifndef LIBRW
#error "POSTFX_HDR needs librw"
#endif

#include "main.h"
#include "RwHelper.h"
#include "Camera.h"
#include "gbuffer.h"
#include "postfx.h"

extern RwRGBA gColourTop;

RwRaster *CGBuffer::pHdrScene;
RwRaster *CGBuffer::pHdrZBuffer;
RwRaster *CGBuffer::pGbufNormalDepth;
RwRaster *CGBuffer::pSavedFrameBuffer;
RwRaster *CGBuffer::pSavedZBuffer;
bool CGBuffer::HdrEnabled = true;
bool CGBuffer::GbufEnabled = true;
bool CGBuffer::bSceneInHDR = false;

// hdrResolve_PS is defined in postfx.cpp (lives in the CPostFX shader pool).

void
CGBuffer::InitOnce(void)
{
}

void
CGBuffer::Open(RwCamera *cam)
{
	if(pHdrScene)
		Close();
	if(!HdrEnabled)
		return;

	int32 w = RwRasterGetWidth(RwCameraGetRaster(cam));
	int32 h = RwRasterGetHeight(RwCameraGetRaster(cam));

	// RGBA16F linear scene RT. Same width/height as the camera (not pow2):
	// only sampled via UV [0,1] in the tonemap-resolve pass.
	int32 colorFmt = (int32)rw::Raster::CAMERATEXTURE | (int32)rw::Raster::F16_RGBA;
	pHdrScene = RwRasterCreate(w, h, 0, colorFmt);

	// Dedicated non-MSAA depth/stencil. librw's Raster::ZBUFFER path always
	// produces a non-MSAA surface, which matches our non-MSAA pHdrScene and
	// avoids the duplicate-texture artefact that comes from binding an
	// MSAA depth surface against a non-MSAA RT.
	pHdrZBuffer = RwRasterCreate(w, h, 0, (int32)rw::Raster::ZBUFFER);

	if(GbufEnabled){
		// Packed: RGB = world normal * 0.5 + 0.5, A = linear view depth.
		// Same format as pHdrScene so D3D9's "matching bit depth" MRT
		// requirement is satisfied on older GPUs.
		pGbufNormalDepth = RwRasterCreate(w, h, 0, colorFmt);
	}
}

void
CGBuffer::Close(void)
{
	if(pHdrScene){ RwRasterDestroy(pHdrScene); pHdrScene = nil; }
	if(pHdrZBuffer){ RwRasterDestroy(pHdrZBuffer); pHdrZBuffer = nil; }
	if(pGbufNormalDepth){ RwRasterDestroy(pGbufNormalDepth); pGbufNormalDepth = nil; }
}

void
CGBuffer::BeginScenePass(RwCamera *cam)
{
	if(!HdrEnabled || pHdrScene == nil || pHdrZBuffer == nil || bSceneInHDR)
		return;

	rw::Camera *rwcam = (rw::Camera*)cam;

	// Stash the originals so EndScenePass can restore them. Replace them
	// in-place — no matrix copy, no projection recalc, so the rendered
	// view is bit-identical to what the main camera would have produced.
	pSavedFrameBuffer = (RwRaster*)rwcam->frameBuffer;
	pSavedZBuffer = (RwRaster*)rwcam->zBuffer;

	RwCameraEndUpdate(cam);

	rwcam->frameBuffer = (rw::Raster*)pHdrScene;
	rwcam->zBuffer = (rw::Raster*)pHdrZBuffer;

	RwCameraBeginUpdate(cam);
	bSceneInHDR = true;

	// Bind the G-buffer MRT slot BEFORE clearing so the clear hits both
	// targets atomically. If we bind after the clear, slot 1 keeps the
	// previous frame's normal/depth — sky/water pixels (which never write
	// to the G-buffer) then feed stale data into SSAO, producing the
	// black smear on the horizon and the "see-through-cars" halo around
	// pedestrians the player reported.
	if(GbufEnabled && pGbufNormalDepth){
		rw::d3d::setMRT(1, (rw::Raster*)pGbufNormalDepth);
		rw::d3d::gbufferEnabled = true;
	}

#ifdef RW_D3D9
	// One Clear call wipes every bound RT (slot 0 and slot 1) plus the
	// depth/stencil. Alpha = 0 in the clear colour means the G-buffer
	// depth channel (slot 1 .a) starts at 0 — SSAO's `if(depth < 0.0001)`
	// guard then correctly skips every untouched pixel.
	DWORD clearCol = D3DCOLOR_ARGB(0,
		(int)gColourTop.red,
		(int)gColourTop.green,
		(int)gColourTop.blue);
	rw::d3d::d3ddevice->Clear(0, nullptr,
		D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
		clearCol, 1.0f, 0);
#else
	RwCameraClear(cam, &gColourTop, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
#endif
}

void
CGBuffer::DropMRT(void)
{
	if(!bSceneInHDR)
		return;
	if(rw::d3d::gbufferEnabled){
		rw::d3d::clearMRT();
		rw::d3d::gbufferEnabled = false;
	}
}

void
CGBuffer::EndScenePass(RwCamera *cam)
{
	if(!bSceneInHDR)
		return;

	DropMRT();

	rw::Camera *rwcam = (rw::Camera*)cam;
	RwCameraEndUpdate(cam);
	rwcam->frameBuffer = (rw::Raster*)pSavedFrameBuffer;
	rwcam->zBuffer = (rw::Raster*)pSavedZBuffer;
	pSavedFrameBuffer = nil;
	pSavedZBuffer = nil;
	RwCameraBeginUpdate(cam);

	bSceneInHDR = false;
}

void
CGBuffer::ResolveTonemap(RwCamera *cam)
{
	// CPostFX::ResolveHDR drives the actual full-screen tonemap pass.
	(void)cam;
}

#endif
