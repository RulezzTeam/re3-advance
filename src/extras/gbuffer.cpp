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
#include "dynamicLights.h"
#include "ibl.h"

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

	// Refresh the procedural IBL constants from CTimeCycle so the ambient
	// term in default_pp_PS reflects this frame's time-of-day. Cheap (a
	// few float copies + four SetPixelShaderConstantF calls), runs once
	// per scene render. When CPostFX::IblEnabled is false the intensity
	// is forced to zero inside librw — the shader contribution cancels.
	CPostFX::UpdateIBL();

	// Phase 2 IBL — update the cube on its refresh schedule and bind the
	// irradiance cube on PS sampler s7 so default_pp_PS picks it up
	// instead of the gradient when iblParams.z is set.
	CIBL::Update(cam);
	CIBL::BindReceiver();

	// Dynamic point lights — pick top-N nearby CPointLights and push to
	// PS c100/c101. Buildings, props, peds (which CEntity::SetupLighting
	// silently skipped) now finally get illuminated by car headlights,
	// lamps, gunfire, and explosions — same as the road has been the
	// whole time.
	CDynamicLights::Update(cam);

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

	// CRITICAL: D3D9 leaves the contents of an "unwritten" MRT slot
	// undefined when the bound PS doesn't emit oColor1. NVIDIA tends to
	// leave it untouched, AMD/Intel sometimes scribble garbage that
	// mimics the slot 0 write. The latter is what produced the visible
	// pedestrian silhouettes on car bodies / water — neoVehicle and the
	// water pass write only oColor0, and the driver was copying that
	// into slot 1, so SSAO sampled "phantom normals" matching the
	// vehicle/water surface.
	//
	// Fix: disable colour-write to slot 1 globally during the HDR pass.
	// defaultRenderCB_Shader and skinRenderCB re-enable it just for the
	// gbuf shader variants, then drop it back to zero. Every other
	// pipeline (vehiclePipe, glossPipe, water, particles, ...) therefore
	// physically cannot touch the G-buffer.
	rw::d3d::d3ddevice->SetRenderState(D3DRS_COLORWRITEENABLE1, 0);
#else
	RwCameraClear(cam, &gColourTop, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
#endif
}

void
CGBuffer::DropMRT(void)
{
	// Idempotent — safe to call whether or not an HDR scene pass is in
	// flight. The previous (bSceneInHDR && gbufferEnabled) double-gate
	// meant a stale COLORWRITEENABLE1=0 could survive an exception unwind
	// that left bSceneInHDR=false but gbufferEnabled=true. We now always
	// restore the colour mask, and only clear the MRT slot when there's
	// actually one to clear.
	if(rw::d3d::gbufferEnabled){
		rw::d3d::clearMRT();
		rw::d3d::gbufferEnabled = false;
	}
#ifdef RW_D3D9
	// ALWAYS restore slot 1 colour-write. The "disabled" mask (0) is a
	// scoped optimisation for the opaque HDR pass; LDR HUD / particle /
	// menu transitions silently fail to write the backbuffer otherwise,
	// because their PS only emits oColor0 but D3D9 still gates writes
	// by the colour-write mask on whatever RTs are bound.
	if(rw::d3d::d3ddevice)
		rw::d3d::d3ddevice->SetRenderState(D3DRS_COLORWRITEENABLE1, 0x0F);
#endif
}

void
CGBuffer::EndScenePass(RwCamera *cam)
{
	// Best-effort cleanup. Run unconditionally so that:
	//   - duplicated EndScenePass calls don't double-restore the camera
	//     framebuffer (the !bSceneInHDR early-out handles that), and
	//   - DropMRT still runs even when bSceneInHDR is false, in case an
	//     earlier exception left COLORWRITEENABLE1 = 0 or the MRT bound.
	// Idempotent — safe in error-handling paths.
	DropMRT();

	if(!bSceneInHDR)
		return;

	// Drop the IBL cube binding so postfx PS slots see a clean s7. The
	// shader stops sampling it because iblParams.z stays at 1 only
	// during the scene draws — gbuf clear happens before the next pass.
	CIBL::UnbindReceiver();

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
CGBuffer::ForceReset(void)
{
	// Belt-and-braces recovery. Called from CPostFX::Close + any place
	// the user can interrupt rendering (menu enter, save / load, mission
	// transition). Doesn't touch the saved framebuffer pointers — the
	// camera's framebuffer is owned by the engine outside our control;
	// restoring it without a valid scene cam would itself break things.
	if(rw::d3d::gbufferEnabled){
		rw::d3d::clearMRT();
		rw::d3d::gbufferEnabled = false;
	}
#ifdef RW_D3D9
	if(rw::d3d::d3ddevice)
		rw::d3d::d3ddevice->SetRenderState(D3DRS_COLORWRITEENABLE1, 0x0F);
#endif
	// Mark the in-HDR flag false so the next BeginScenePass picks up the
	// camera framebuffer cleanly. If the scene cam is mid-pass when this
	// is called, the worst that happens is a one-frame flash.
	bSceneInHDR = false;
}

void
CGBuffer::ResolveTonemap(RwCamera *cam)
{
	// CPostFX::ResolveHDR drives the actual full-screen tonemap pass.
	(void)cam;
}

#endif
