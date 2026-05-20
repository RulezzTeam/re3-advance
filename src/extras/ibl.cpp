#define WITHD3D
#include "common.h"

#ifdef POSTFX_HDR

#ifndef LIBRW
#error "POSTFX_HDR needs librw"
#endif

#include "main.h"
#include "RwHelper.h"
#include "Camera.h"
#include "Timecycle.h"
#include "Weather.h"
#include "Lights.h"
#include "postfx.h"
#include "ibl.h"
// librw graphics log — vendor/librw-aap is on the include path via
// premake5.lua's `includedirs { Librw }`.
#include "src/rwlog.h"

extern RwRGBAReal DirectionalLightColourForFrame;

void *CIBL::captureCube;
void *CIBL::irradianceCube;
void *CIBL::brdfLut;	// 256×256 F16_RGBA RwRaster — baked once at Open
bool CIBL::Enabled = false;	// opt-in; gradient IBL stays the default
int CIBL::FrameCounter = 0;
float CIBL::ReflStrength = 1.0f;
// Defaults bumped vs the original Phase-1 picks: 128² capture (was 64) +
// 64² irradiance (was 32). Memory cost ≈ 3 MB total at 128 capture vs
// ~0.75 MB at 64 — negligible against any modern VRAM budget — and the
// captured sky / sun disc reads cleanly at the higher res so reflections
// on cars + irradiance gradient on buildings stop looking blocky.
int32 CIBL::CaptureSize = 128;
int32 CIBL::IrradianceSize = 64;
int8  CIBL::CaptureSizeIndex = 1;     // 0..3 = 64/128/256/512 — default "Standard (128)"
int8  CIBL::IrradianceSizeIndex = 2;  // 0..3 = 16/32/64/128 — default "Large (64)"

void
CIBL::CaptureSizeAfterChange(int8 before, int8 after)
{
	(void)before;
	static const int32 kSizeTable[4] = { 64, 128, 256, 512 };
	int8 idx = after;
	if(idx < 0) idx = 0;
	if(idx > 3) idx = 3;
	CaptureSize = kSizeTable[idx];
	Reopen();
}

void
CIBL::IrradianceSizeAfterChange(int8 before, int8 after)
{
	(void)before;
	static const int32 kSizeTable[4] = { 16, 32, 64, 128 };
	int8 idx = after;
	if(idx < 0) idx = 0;
	if(idx > 3) idx = 3;
	IrradianceSize = kSizeTable[idx];
	Reopen();
}

#ifdef RW_D3D9
static void *iblSkyToCube_PS;
static void *iblConvolve_PS;
static void *cubePass_VS;
// Split-sum BRDF LUT bake PS — Stage 12 P1. Runs once at Open to
// populate CIBL::brdfLut.
static void *brdfLut_PS;
// Latches the first successful bake so a subsequent Update doesn't
// keep re-baking the same content. Reset to false in Close.
static bool sBrdfLutBaked = false;
static IDirect3DVertexDeclaration9 *cubeQuadDecl;

// NDC-space fullscreen quad — bypasses librw's im2d entirely. Direct
// D3D9 vertex format paired with cubePass_VS, so the rasterizer sees a
// quad that exactly covers the destination viewport (= cube face)
// regardless of what scene camera (if any) the engine has set.
//
// Position is already in NDC (-1..1). UV (0..1) is interpolated to the
// PS. Two triangles in counter-clockwise order so D3D9's default CCW
// cull doesn't drop them.
struct CubeQuadVert {
	float x, y, z;
	float u, v;
};

static const CubeQuadVert kCubeQuadVerts[4] = {
	{ -1.0f, +1.0f, 0.0f,    0.0f, 0.0f },	// TL
	{ +1.0f, +1.0f, 0.0f,    1.0f, 0.0f },	// TR
	{ +1.0f, -1.0f, 0.0f,    1.0f, 1.0f },	// BR
	{ -1.0f, -1.0f, 0.0f,    0.0f, 1.0f },	// BL
};
static const uint16 kCubeQuadIdx[6] = { 0, 1, 2, 0, 2, 3 };

static bool
ensureCubeQuadDecl(void)
{
	if(cubeQuadDecl) return true;
	if(rw::d3d::d3ddevice == nullptr) return false;
	D3DVERTEXELEMENT9 elements[] = {
		{ 0,  0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
		{ 0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
		D3DDECL_END()
	};
	HRESULT hr = rw::d3d::d3ddevice->CreateVertexDeclaration(elements, &cubeQuadDecl);
	if(FAILED(hr) || cubeQuadDecl == nullptr){
		cubeQuadDecl = nullptr;
		return false;
	}
	return true;
}

// D3DCUBEMAP_FACES — order: +X, -X, +Y, -Y, +Z, -Z.
// For each face: the forward direction (face normal), right vector, up vector.
// Right-handed D3D9 cubemap convention.
static const float cubeFaceBasis[6][9] = {
	// +X (right):    F=(+1,0,0)   R=(0,0,-1)   U=(0,+1,0)
	{ 1,0,0,    0,0,-1,    0,1,0 },
	// -X (left):     F=(-1,0,0)   R=(0,0,+1)   U=(0,+1,0)
	{ -1,0,0,   0,0,1,     0,1,0 },
	// +Y (up):       F=(0,+1,0)   R=(+1,0,0)   U=(0,0,-1)
	{ 0,1,0,    1,0,0,     0,0,-1 },
	// -Y (down):     F=(0,-1,0)   R=(+1,0,0)   U=(0,0,+1)
	{ 0,-1,0,   1,0,0,     0,0,1 },
	// +Z (forward):  F=(0,0,+1)   R=(+1,0,0)   U=(0,+1,0)
	{ 0,0,1,    1,0,0,     0,1,0 },
	// -Z (back):     F=(0,0,-1)   R=(-1,0,0)   U=(0,+1,0)
	{ 0,0,-1,   -1,0,0,    0,1,0 },
};
#endif

void
CIBL::InitOnce(void)
{
	captureCube = nil;
	irradianceCube = nil;
	FrameCounter = 0;
}

void
CIBL::Open(RwCamera *cam)
{
	(void)cam;
#ifdef RW_D3D9
	if(captureCube != nil)
		Close();
	if(!Enabled){
		rwLogf(rw::RW_LOG_INFO, "CIBL::Open — disabled, skipping cube allocation");
		return;
	}

	// Allocate the source + irradiance cubes. createCubeTexture returns
	// nullptr cleanly on caps/format reject so we don't need to manage
	// fallback here — just check + log + disable.
	int colorFmt = (int)rw::Raster::F16_RGBA;
	captureCube    = rw::d3d::createCubeTexture(CaptureSize, colorFmt);
	irradianceCube = rw::d3d::createCubeTexture(IrradianceSize, colorFmt);
	if(captureCube == nullptr || irradianceCube == nullptr){
		if(captureCube){ rw::d3d::destroyCubeTexture(captureCube); captureCube = nil; }
		if(irradianceCube){ rw::d3d::destroyCubeTexture(irradianceCube); irradianceCube = nil; }
		Enabled = false;
		rwLogf(rw::RW_LOG_WARN, "CIBL::Open — cube creation failed, disabling (gradient IBL still works)");
		return;
	}

	// Lazy-load shaders the first time we open. Stay loaded across
	// game-state transitions.
	if(iblSkyToCube_PS == nullptr){
		#include "shaders/obj/iblSkyToCube_PS.inc"
		iblSkyToCube_PS = rw::d3d::createPixelShader(iblSkyToCube_PS_cso);
	}
	if(iblConvolve_PS == nullptr){
		#include "shaders/obj/iblConvolve_PS.inc"
		iblConvolve_PS = rw::d3d::createPixelShader(iblConvolve_PS_cso);
	}
	if(cubePass_VS == nullptr){
		#include "shaders/obj/cubePass_VS.inc"
		cubePass_VS = rw::d3d::createVertexShader(cubePass_VS_cso);
	}
	if(brdfLut_PS == nullptr){
		#include "shaders/obj/brdfLut_PS.inc"
		brdfLut_PS = rw::d3d::createPixelShader(brdfLut_PS_cso);
	}
	if(iblSkyToCube_PS == nullptr || iblConvolve_PS == nullptr || cubePass_VS == nullptr){
		rwLogf(rw::RW_LOG_ERROR, "CIBL::Open — shader creation failed, disabling");
		Close();
		Enabled = false;
		return;
	}

	// Allocate the BRDF LUT raster (256×256 F16_RGBA — the .b/.a channels
	// stay zero; we'd use a 2-channel format if librw exposed one, but
	// F16_RGBA is the only float texture format on the librw side and the
	// extra 4 bytes × 65536 pixels = 256 KB of "wasted" VRAM is trivial).
	// brdfLut_PS bake is deferred until the first CIBL::Update — same as
	// the cube faces — so engine-level state (BeginUpdate, viewport, etc)
	// is in a known-good condition when we run the direct-D3D9 dispatch.
	if(brdfLut == nil){
		int32 lutFmt = (int32)rw::Raster::CAMERATEXTURE | (int32)rw::Raster::F16_RGBA;
		brdfLut = RwRasterCreate(256, 256, 0, lutFmt);
		if(brdfLut == nil)
			rwLogf(rw::RW_LOG_WARN, "CIBL::Open — brdfLut allocation failed; split-sum path will fall back to analytic Fresnel");
	}
	sBrdfLutBaked = false;

	// Build the vertex declaration up front so the first Update doesn't
	// have to. Failure here is non-fatal — renderCubeFace also retries.
	if(!ensureCubeQuadDecl())
		rwLogf(rw::RW_LOG_WARN, "CIBL::Open — cubeQuadDecl creation deferred to first Update");

	// First-frame populate is DEFERRED. Open runs from MotionBlurOpen
	// before engine->currentCamera or any RwCameraBeginUpdate has fired
	// — renderCubeFace works directly with d3ddevice and survives that,
	// but other engine state (lights, time-cycle, sun direction) may
	// not be wired up yet. Set FrameCounter so the first BeginScenePass
	// triggers Update with a fully-initialised engine.
	FrameCounter = REFRESH_PERIOD;

	// Register the cubes with librw so they survive Alt-Tab device
	// lost/reset. librw will Release them on lost + CreateCubeTexture
	// them back on reset, writing the new handle into the same
	// captureCube/irradianceCube storage we own.
	rw::d3d::registerVidmemCube((IDirect3DCubeTexture9**)&captureCube, CaptureSize, colorFmt);
	rw::d3d::registerVidmemCube((IDirect3DCubeTexture9**)&irradianceCube, IrradianceSize, colorFmt);

	rwLogf(rw::RW_LOG_INFO, "CIBL::Open OK — captureCube=%d irradianceCube=%d (Update deferred to first frame, registered with vidmemCubes)",
	    CaptureSize, IrradianceSize);
#endif
}

void
CIBL::Close(void)
{
#ifdef RW_D3D9
	// Unregister from vidmemCubes BEFORE destroying so the device-lost
	// path doesn't try to recreate a cube we're about to delete.
	rw::d3d::unregisterVidmemCube((IDirect3DCubeTexture9**)&captureCube);
	rw::d3d::unregisterVidmemCube((IDirect3DCubeTexture9**)&irradianceCube);
	if(captureCube){ rw::d3d::destroyCubeTexture(captureCube); captureCube = nil; }
	if(irradianceCube){ rw::d3d::destroyCubeTexture(irradianceCube); irradianceCube = nil; }
	// BRDF LUT — RwRasterDestroy is safe even on nil. Bake-latch reset
	// so the next Open starts from a clean slate.
	if(brdfLut){ RwRasterDestroy((RwRaster*)brdfLut); brdfLut = nil; }
	sBrdfLutBaked = false;
	// Shaders stay loaded — they have no per-scene state and re-loading
	// them on every game-state transition would just churn.
	// cubeQuadDecl stays alive too; D3D9 vertex declarations are tiny
	// and survive device resets unchanged.
	rwLogf(rw::RW_LOG_INFO, "CIBL::Close");
#endif
}

void
CIBL::Reopen(void)
{
	// Hook for the menu's CCFOSelect AfterChange — drops the current cubes
	// and re-allocates at the new CaptureSize / IrradianceSize. Safe to
	// call even when CIBL is disabled (Close handles a null cube cleanly,
	// Open early-outs on !Enabled). The first frame after the call will
	// trigger an Update that re-bakes the sky into the new cube.
#ifdef RW_D3D9
	if(captureCube != nil || irradianceCube != nil)
		Close();
	if(Enabled)
		Open(nullptr);	// cam isn't actually used by Open's allocation path
#endif
}

#ifdef RW_D3D9
// Direct D3D9 cube-face dispatch.
//
// CRITICAL: do NOT go through librw's RwIm2DRenderIndexedPrimitive here.
// Two reasons:
//   1. im2DSetXform reads engine->currentCamera->frameBuffer->width to
//      build the screen→NDC transform. Open-time runs from MotionBlurOpen
//      ← CameraSize ← AppEventHandler — currentCamera is nullptr there,
//      so the deref crashes.
//   2. Even if currentCamera were valid, that transform scales the quad
//      by the scene framebuffer (1920×1080 typical) — but we're drawing
//      into a 64×64 cube face, so the rasterizer would see a 3%-of-face
//      micro-quad and the rest stays unwritten.
//
// Direct path: GetRenderTarget+SetRenderTarget to swap RT slot 0 to the
// face surface, set viewport to face size, push our own minimal
// passthrough VS + the caller's PS, draw 2 triangles in NDC space via
// DrawIndexedPrimitiveUP. Every D3D9 call is HRESULT-checked; any
// failure leaves the device state restored before returning.
//
// Save/restore: RT slot 0, depth-stencil surface, viewport, vertex
// shader, pixel shader, vertex declaration. Each Get* returns an
// AddRef'd handle that we Release after the corresponding Set*-back.
static void
renderCubeFace(void *dstCube, int face, float size, void *ps,
               const float *constsC10, int constCount)
{
	if(dstCube == nullptr || ps == nullptr || size <= 0.0f)
		return;
	if(cubePass_VS == nullptr){
		rwLogf(rw::RW_LOG_ERROR, "renderCubeFace: cubePass_VS not loaded");
		return;
	}
	if(!ensureCubeQuadDecl()){
		rwLogf(rw::RW_LOG_ERROR, "renderCubeFace: ensureCubeQuadDecl failed");
		return;
	}

	IDirect3DDevice9 *dev = rw::d3d::d3ddevice;
	if(dev == nullptr){
		rwLogf(rw::RW_LOG_ERROR, "renderCubeFace: d3ddevice null");
		return;
	}

	// Acquire destination face surface (AddRef'd).
	IDirect3DSurface9 *dstSurf = nullptr;
	HRESULT hr = ((IDirect3DCubeTexture9*)dstCube)->GetCubeMapSurface((D3DCUBEMAP_FACES)face, 0, &dstSurf);
	if(FAILED(hr) || dstSurf == nullptr){
		rwLogf(rw::RW_LOG_ERROR, "renderCubeFace: GetCubeMapSurface face=%d hr=0x%08lX",
		    face, (unsigned long)hr);
		return;
	}

	// Save EVERY piece of device state we touch (all Get* are AddRef'd).
	IDirect3DSurface9           *savedRT   = nullptr;
	IDirect3DSurface9           *savedDS   = nullptr;
	IDirect3DVertexShader9      *savedVS   = nullptr;
	IDirect3DPixelShader9       *savedPS   = nullptr;
	IDirect3DVertexDeclaration9 *savedDecl = nullptr;
	D3DVIEWPORT9                 savedVp   = {};
	DWORD savedZEnable      = 0;
	DWORD savedZWriteEnable = 0;
	DWORD savedAlphaBlend   = 0;
	DWORD savedCullMode     = D3DCULL_CCW;
	DWORD savedColorWrite   = 0x0F;

	dev->GetRenderTarget(0, &savedRT);
	dev->GetDepthStencilSurface(&savedDS);
	dev->GetVertexShader(&savedVS);
	dev->GetPixelShader(&savedPS);
	dev->GetVertexDeclaration(&savedDecl);
	dev->GetViewport(&savedVp);
	dev->GetRenderState(D3DRS_ZENABLE,          &savedZEnable);
	dev->GetRenderState(D3DRS_ZWRITEENABLE,     &savedZWriteEnable);
	dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &savedAlphaBlend);
	dev->GetRenderState(D3DRS_CULLMODE,         &savedCullMode);
	dev->GetRenderState(D3DRS_COLORWRITEENABLE, &savedColorWrite);

	// Bind new RT + viewport.
	bool ok = true;
	if(FAILED(dev->SetRenderTarget(0, dstSurf))){
		rwLogf(rw::RW_LOG_ERROR, "renderCubeFace: SetRenderTarget failed face=%d", face);
		ok = false;
	}
	if(ok && FAILED(dev->SetDepthStencilSurface(nullptr))){
		// Not fatal — we just leave the old DS bound. Cube render
		// doesn't write depth so it's harmless either way.
		rwLogf(rw::RW_LOG_WARN, "renderCubeFace: SetDepthStencilSurface(nullptr) failed");
	}
	if(ok){
		D3DVIEWPORT9 vp = {};
		vp.X      = 0;
		vp.Y      = 0;
		vp.Width  = (DWORD)size;
		vp.Height = (DWORD)size;
		vp.MinZ   = 0.0f;
		vp.MaxZ   = 1.0f;
		if(FAILED(dev->SetViewport(&vp))){
			rwLogf(rw::RW_LOG_ERROR, "renderCubeFace: SetViewport failed face=%d", face);
			ok = false;
		}
	}

	if(ok){
		// No depth, no blend, no cull, full-channel colour write.
		dev->SetRenderState(D3DRS_ZENABLE,          FALSE);
		dev->SetRenderState(D3DRS_ZWRITEENABLE,     FALSE);
		dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		dev->SetRenderState(D3DRS_CULLMODE,         D3DCULL_NONE);
		dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);

		// Upload caller's PS constants starting at c10.
		if(constsC10 && constCount > 0){
			HRESULT hcr = dev->SetPixelShaderConstantF(10, constsC10, constCount);
			if(FAILED(hcr))
				rwLogf(rw::RW_LOG_WARN, "renderCubeFace: SetPSConst slot=10 count=%d hr=0x%08lX",
				    constCount, (unsigned long)hcr);
		}

		// Bind our minimal VS + caller's PS + matching decl.
		dev->SetVertexShader((IDirect3DVertexShader9*)cubePass_VS);
		dev->SetPixelShader((IDirect3DPixelShader9*)ps);
		dev->SetVertexDeclaration(cubeQuadDecl);

		HRESULT dhr = dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,
		    0, 4, 2,
		    kCubeQuadIdx, D3DFMT_INDEX16,
		    kCubeQuadVerts, sizeof(CubeQuadVert));
		if(FAILED(dhr))
			rwLogf(rw::RW_LOG_ERROR, "renderCubeFace: DrawIndexedPrimitiveUP face=%d hr=0x%08lX",
			    face, (unsigned long)dhr);
	}

	// Restore EVERY piece of state we touched. Order doesn't matter
	// strictly, but we restore the heaviest binding (RT) last so any
	// intermediate Set* failures don't leak the wrong RT into the
	// next pass.
	dev->SetVertexShader(savedVS);
	dev->SetPixelShader(savedPS);
	dev->SetVertexDeclaration(savedDecl);
	dev->SetViewport(&savedVp);
	dev->SetDepthStencilSurface(savedDS);
	dev->SetRenderTarget(0, savedRT);
	dev->SetRenderState(D3DRS_ZENABLE,          savedZEnable);
	dev->SetRenderState(D3DRS_ZWRITEENABLE,     savedZWriteEnable);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, savedAlphaBlend);
	dev->SetRenderState(D3DRS_CULLMODE,         savedCullMode);
	dev->SetRenderState(D3DRS_COLORWRITEENABLE, savedColorWrite);

	// Release every AddRef'd handle we acquired.
	if(savedVS)   savedVS->Release();
	if(savedPS)   savedPS->Release();
	if(savedDecl) savedDecl->Release();
	if(savedRT)   savedRT->Release();
	if(savedDS)   savedDS->Release();
	dstSurf->Release();
}

// Bake the split-sum BRDF LUT into CIBL::brdfLut. Mirrors the
// renderCubeFace state-save/restore pattern but writes to a 2D RwRaster
// instead of a cube face. Called once from CIBL::Update on the first
// frame after Open — the LUT is shader-math-only so a single bake is
// correct for the entire session.
void
CIBL::BakeBrdfLut(void)
{
#ifdef RW_D3D9
	if(brdfLut == nil || brdfLut_PS == nullptr || cubePass_VS == nullptr){
		rwLogf(rw::RW_LOG_ERROR, "CIBL::BakeBrdfLut — prerequisites missing");
		return;
	}
	if(!ensureCubeQuadDecl()){
		rwLogf(rw::RW_LOG_ERROR, "CIBL::BakeBrdfLut — cubeQuadDecl creation failed");
		return;
	}

	// Get the underlying D3D9 surface from the librw raster. Same
	// pattern as BindRasterToSampler — natras->texture is the
	// IDirect3DTexture9, GetSurfaceLevel(0) gives the mip-0 surface
	// we render into.
	rw::Raster *raster = (rw::Raster*)brdfLut;
	if(raster->parent) raster = raster->parent;
	rw::d3d::D3dRaster *natras = GETD3DRASTEREXT(raster);
	if(natras == nullptr || natras->texture == nullptr){
		rwLogf(rw::RW_LOG_ERROR, "CIBL::BakeBrdfLut — no natras texture");
		return;
	}
	IDirect3DTexture9 *tex = (IDirect3DTexture9*)natras->texture;
	IDirect3DSurface9 *dstSurf = nullptr;
	if(FAILED(tex->GetSurfaceLevel(0, &dstSurf)) || dstSurf == nullptr){
		rwLogf(rw::RW_LOG_ERROR, "CIBL::BakeBrdfLut — GetSurfaceLevel(0) failed");
		return;
	}

	IDirect3DDevice9 *dev = rw::d3d::d3ddevice;

	// Save the same set of device state as renderCubeFace.
	IDirect3DSurface9           *savedRT   = nullptr;
	IDirect3DSurface9           *savedDS   = nullptr;
	IDirect3DVertexShader9      *savedVS   = nullptr;
	IDirect3DPixelShader9       *savedPS   = nullptr;
	IDirect3DVertexDeclaration9 *savedDecl = nullptr;
	D3DVIEWPORT9                 savedVp   = {};
	DWORD savedZEnable      = 0;
	DWORD savedZWriteEnable = 0;
	DWORD savedAlphaBlend   = 0;
	DWORD savedCullMode     = D3DCULL_CCW;
	DWORD savedColorWrite   = 0x0F;

	dev->GetRenderTarget(0, &savedRT);
	dev->GetDepthStencilSurface(&savedDS);
	dev->GetVertexShader(&savedVS);
	dev->GetPixelShader(&savedPS);
	dev->GetVertexDeclaration(&savedDecl);
	dev->GetViewport(&savedVp);
	dev->GetRenderState(D3DRS_ZENABLE,          &savedZEnable);
	dev->GetRenderState(D3DRS_ZWRITEENABLE,     &savedZWriteEnable);
	dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &savedAlphaBlend);
	dev->GetRenderState(D3DRS_CULLMODE,         &savedCullMode);
	dev->GetRenderState(D3DRS_COLORWRITEENABLE, &savedColorWrite);

	bool ok = true;
	if(FAILED(dev->SetRenderTarget(0, dstSurf))){
		rwLogf(rw::RW_LOG_ERROR, "CIBL::BakeBrdfLut — SetRenderTarget failed");
		ok = false;
	}
	if(ok) dev->SetDepthStencilSurface(nullptr);

	if(ok){
		D3DVIEWPORT9 vp = {};
		vp.X = 0; vp.Y = 0; vp.Width = 256; vp.Height = 256;
		vp.MinZ = 0.0f; vp.MaxZ = 1.0f;
		dev->SetViewport(&vp);
		dev->SetRenderState(D3DRS_ZENABLE,          FALSE);
		dev->SetRenderState(D3DRS_ZWRITEENABLE,     FALSE);
		dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		dev->SetRenderState(D3DRS_CULLMODE,         D3DCULL_NONE);
		dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);

		dev->SetVertexShader((IDirect3DVertexShader9*)cubePass_VS);
		dev->SetPixelShader((IDirect3DPixelShader9*)brdfLut_PS);
		dev->SetVertexDeclaration(cubeQuadDecl);

		HRESULT dhr = dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,
		    0, 4, 2,
		    kCubeQuadIdx, D3DFMT_INDEX16,
		    kCubeQuadVerts, sizeof(CubeQuadVert));
		if(FAILED(dhr))
			rwLogf(rw::RW_LOG_ERROR, "CIBL::BakeBrdfLut — DrawIndexedPrimitiveUP hr=0x%08lX",
			    (unsigned long)dhr);
		else
			rwLogf(rw::RW_LOG_INFO, "CIBL::BakeBrdfLut — 256² split-sum LUT baked");
	}

	dev->SetVertexShader(savedVS);
	dev->SetPixelShader(savedPS);
	dev->SetVertexDeclaration(savedDecl);
	dev->SetViewport(&savedVp);
	dev->SetDepthStencilSurface(savedDS);
	dev->SetRenderTarget(0, savedRT);
	dev->SetRenderState(D3DRS_ZENABLE,          savedZEnable);
	dev->SetRenderState(D3DRS_ZWRITEENABLE,     savedZWriteEnable);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, savedAlphaBlend);
	dev->SetRenderState(D3DRS_CULLMODE,         savedCullMode);
	dev->SetRenderState(D3DRS_COLORWRITEENABLE, savedColorWrite);

	if(savedVS)   savedVS->Release();
	if(savedPS)   savedPS->Release();
	if(savedDecl) savedDecl->Release();
	if(savedRT)   savedRT->Release();
	if(savedDS)   savedDS->Release();
	dstSurf->Release();
#endif
}
#endif

void
CIBL::Update(RwCamera *cam)
{
	(void)cam;
#ifdef RW_D3D9
	if(!Enabled || captureCube == nil || irradianceCube == nil)
		return;
	if(iblSkyToCube_PS == nil || iblConvolve_PS == nil || cubePass_VS == nil)
		return;
	// Defensive: even though renderCubeFace works without the engine's
	// active camera, the surrounding code (sun direction from pDirect,
	// CTimeCycle colours) requires engine state. If we're called before
	// the first RwCameraBeginUpdate fully wires the engine, defer one
	// more frame.
	if(rw::engine == nullptr || rw::d3d::d3ddevice == nullptr)
		return;

	// Bake the BRDF LUT on the very first Update — exactly once, then
	// the latch suppresses further rebakes. The LUT only depends on
	// shader math (no scene state), so a single bake is correct for
	// the entire session.
	if(!sBrdfLutBaked && brdfLut && brdfLut_PS){
		BakeBrdfLut();
		sBrdfLutBaked = true;
	}

	FrameCounter++;
	if(FrameCounter < REFRESH_PERIOD)
		return;
	FrameCounter = 0;

	// === Pass 1: write the analytic sky into each face of captureCube. ===
	// Sky colours follow CTimeCycle so the cube tracks day-night.
	float exposure = CPostFX::IblExposure;
	float skyTop[4] = {
		(float)CTimeCycle::GetSkyTopRed()   * exposure,
		(float)CTimeCycle::GetSkyTopGreen() * exposure,
		(float)CTimeCycle::GetSkyTopBlue()  * exposure,
		0.0f,
	};
	float skyHor[4] = {
		(float)CTimeCycle::GetSkyBottomRed()   * exposure,
		(float)CTimeCycle::GetSkyBottomGreen() * exposure,
		(float)CTimeCycle::GetSkyBottomBlue()  * exposure,
		0.0f,
	};
	float skyGnd[4] = { 0.06f, 0.055f, 0.05f, 0.0f };

	// Sun direction (toward the sun) — same as the volumetric fog input.
	float sunDir[4] = { 0, 0, 1, 0 };
	if(pDirect){
		rw::V3d a = pDirect->getFrame()->getLTM()->at;
		float len = sqrtf(a.x*a.x + a.y*a.y + a.z*a.z);
		if(len > 1e-5f){
			sunDir[0] = -a.x / len;
			sunDir[1] = -a.y / len;
			sunDir[2] = -a.z / len;
		}
	}
	// Sun colour (HDR) + cosine of sun-disk radius. Bigger radius = larger
	// disk in the cube; ~0.9995 ≈ 1.8° half-angle, comparable to the
	// real sun's apparent size at noon.
	float sunCol[4] = {
		DirectionalLightColourForFrame.red   * 1.5f,
		DirectionalLightColourForFrame.green * 1.5f,
		DirectionalLightColourForFrame.blue  * 1.5f,
		0.9995f,
	};

	for(int face = 0; face < 6; face++){
		const float *b = cubeFaceBasis[face];
		float faceF[4] = { b[0], b[1], b[2], 0 };
		float faceR[4] = { b[3], b[4], b[5], 0 };
		float faceU[4] = { b[6], b[7], b[8], 0 };
		float consts[8 * 4];
		// c10 face F, c11 face R, c12 face U, c13 skyTop, c14 skyHor,
		// c15 skyGnd, c16 sunDir, c17 sunCol — 8 vec4s contiguous.
		memcpy(consts + 0*4, faceF, sizeof(float)*4);
		memcpy(consts + 1*4, faceR, sizeof(float)*4);
		memcpy(consts + 2*4, faceU, sizeof(float)*4);
		memcpy(consts + 3*4, skyTop, sizeof(float)*4);
		memcpy(consts + 4*4, skyHor, sizeof(float)*4);
		memcpy(consts + 5*4, skyGnd, sizeof(float)*4);
		memcpy(consts + 6*4, sunDir, sizeof(float)*4);
		memcpy(consts + 7*4, sunCol, sizeof(float)*4);
		renderCubeFace(captureCube, face, (float)CaptureSize,
		               iblSkyToCube_PS, consts, 8);
	}

	// === Pass 2: convolve captureCube → irradianceCube. ===
	// Source cube is bound on s0 inside the convolve PS.
	rw::d3d::bindCubeToSampler(0, captureCube);
	for(int face = 0; face < 6; face++){
		const float *b = cubeFaceBasis[face];
		float faceF[4] = { b[0], b[1], b[2], 0 };
		float faceR[4] = { b[3], b[4], b[5], 0 };
		float faceU[4] = { b[6], b[7], b[8], 0 };
		float consts[3 * 4];
		memcpy(consts + 0*4, faceF, sizeof(float)*4);
		memcpy(consts + 1*4, faceR, sizeof(float)*4);
		memcpy(consts + 2*4, faceU, sizeof(float)*4);
		renderCubeFace(irradianceCube, face, (float)IrradianceSize,
		               iblConvolve_PS, consts, 3);
	}
	rw::d3d::bindCubeToSampler(0, nil);
#endif
}

void
CIBL::BindReceiver(void)
{
#ifdef RW_D3D9
	if(Enabled && irradianceCube)
		rw::d3d::bindCubeToSampler(7, irradianceCube);
	// Bind the capture cube as the reflection cube on s8. This drives
	// the Fresnel-weighted specular reflection term in default_pp_PS
	// — every reflective surface (buildings, road, peds) now picks up
	// the live sky+sun. Strength gated by iblReflParams.x = ReflStrength
	// (default 1.0, capped by surfSpecular per material).
	if(Enabled && captureCube)
		rw::d3d::bindCubeToSampler(8, captureCube);
	// Bind the split-sum BRDF LUT on s11. Only after the bake latch is
	// set; pre-bake the texture contains undefined RT noise that would
	// add visible artefacts to the spec term. The reflParams.y flag tells
	// the receiver whether to use the split-sum path or the legacy
	// analytic Fresnel.
	if(Enabled && brdfLut != nil && sBrdfLutBaked){
		rw::Raster *raster = (rw::Raster*)brdfLut;
		if(raster->parent) raster = raster->parent;
		rw::d3d::D3dRaster *natras = GETD3DRASTEREXT(raster);
		if(natras && natras->texture){
			rw::d3d::d3ddevice->SetTexture(11, (IDirect3DTexture9*)natras->texture);
			rw::d3d::d3ddevice->SetSamplerState(11, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			rw::d3d::d3ddevice->SetSamplerState(11, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			rw::d3d::d3ddevice->SetSamplerState(11, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
			rw::d3d::d3ddevice->SetSamplerState(11, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			rw::d3d::d3ddevice->SetSamplerState(11, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		}
	}
	// iblReflParams.y = split-sum enable flag (0 = legacy analytic
	// Fresnel, 1 = use LUT). Receiver gates the path on this so a
	// failed bake (LUT raster nil, shader missing) falls back cleanly.
	float lutFlag = (Enabled && brdfLut != nil && sBrdfLutBaked) ? 1.0f : 0.0f;
	float reflParams[4] = { Enabled ? ReflStrength : 0.0f, lutFlag, 0, 0 };
	rw::d3d::d3ddevice->SetPixelShaderConstantF(64, reflParams, 1);
#endif
}

void
CIBL::UnbindReceiver(void)
{
#ifdef RW_D3D9
	rw::d3d::bindCubeToSampler(7, nil);
	rw::d3d::bindCubeToSampler(8, nil);
	rw::d3d::d3ddevice->SetTexture(11, nil);
	// Force reflection strength to 0 so non-pp passes don't accidentally
	// pull from the (now-unbound) sampler.
	float zero[4] = { 0, 0, 0, 0 };
	rw::d3d::d3ddevice->SetPixelShaderConstantF(64, zero, 1);
#endif
}

// External hook for vehiclePipe (custompipes_d3d9.cpp) — returns the
// captureCube handle when CIBL is enabled and populated. Kept as a free
// function so we don't have to drag the full ibl.h into custompipes.
extern "C" void*
CIBL_GetCaptureCube(void)
{
	return CIBL::Enabled ? CIBL::captureCube : nullptr;
}

#endif
