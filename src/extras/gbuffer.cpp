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

	// Clear with the time-of-day sky colour and full depth. Without this
	// pHdrScene retains last-frame pixels for any region not written this
	// frame (sky band, transparent windows) — ghost-duplicate textures.
	RwCameraClear(cam, &gColourTop, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);

	RwCameraBeginUpdate(cam);
	bSceneInHDR = true;

	if(GbufEnabled && pGbufNormalDepth){
		rw::d3d::setMRT(1, (rw::Raster*)pGbufNormalDepth);
		rw::d3d::gbufferEnabled = true;
	}
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
