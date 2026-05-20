#pragma once

#ifdef POSTFX_HDR

// CGBuffer owns the HDR scene render target and (when POSTFX_GBUFFER is
// active) the packed normal+depth MRT. The class drives a simple lifecycle:
//
//   CPostFX::Open(cam) -> CGBuffer::Open(cam)
//   per-frame:
//     BeginScenePass(cam) - redirect Scene.camera->frameBuffer to pHdrScene
//                            + bind MRT slot 1 to pGbufNormalDepth
//     <render scene into HDR + G-buffer>
//     DropMRT()           - call before alpha/particles (single-RT writes)
//     EndScenePass(cam)   - restore camera framebuffer to LDR backbuffer
//     ResolveTonemap(cam) - full-screen tonemap from pHdrScene to LDR
//   CPostFX::Close()   -> CGBuffer::Close()
//
// HdrEnabled controls whether any of the work runs. GbufEnabled enables the
// secondary normal+depth MRT (gated separately because the G-buffer is only
// useful for SSAO/CSM/TAA consumers that aren't online yet).

class CGBuffer
{
public:
	static RwRaster *pHdrScene;
	static RwRaster *pHdrZBuffer;		// non-MSAA depth/stencil paired with pHdrScene
	static RwRaster *pGbufNormalDepth;
	// During BeginScenePass we *don't* swap to a separate camera (that path
	// caused subtle projection drift because librw recomputes devView/devProj
	// from the camera's frame inside beginUpdate). Instead we stash the
	// camera's original frameBuffer + zBuffer here and replace them with
	// pHdrScene/pHdrZBuffer in-place, so all the camera's matrices stay
	// exactly the same.
	static RwRaster *pSavedFrameBuffer;
	static RwRaster *pSavedZBuffer;
	static bool HdrEnabled;
	static bool GbufEnabled;
	static bool bSceneInHDR;		// true between Begin/EndScenePass

	static void InitOnce(void);
	static void Open(RwCamera *cam);
	static void Close(void);

	// Called once per frame, between RenderScene() and CPostFX::Render():
	static void BeginScenePass(RwCamera *cam);
	static void DropMRT(void);
	static void EndScenePass(RwCamera *cam);
	static void ResolveTonemap(RwCamera *cam);

	// Idempotent recovery — drops any latched MRT state and forces
	// COLORWRITEENABLE1 back to 0x0F so the next LDR pass isn't blocked
	// from writing the colour channel. Called from CPostFX::Close, and
	// safe to call from any error-handling path (menu enter, mission
	// load, save/load, exception unwind). Survives being called when
	// CGBuffer isn't open + when an HDR pass was never started, so the
	// caller never has to nullcheck.
	static void ForceReset(void);
};

// Forward declared shader pointer for the dedicated HDR-resolve PS that
// shares the colourfilterVC pipeline but reads RGBA16F instead of LDR.
extern void *hdrResolve_PS;

#endif
