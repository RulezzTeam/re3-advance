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

extern RwRGBAReal DirectionalLightColourForFrame;

void *CIBL::captureCube;
void *CIBL::irradianceCube;
bool CIBL::Enabled = false;	// opt-in; gradient IBL stays the default
int CIBL::FrameCounter = 0;

#ifdef RW_D3D9
static void *iblSkyToCube_PS;
static void *iblConvolve_PS;

// 4-vertex fullscreen quad sized to the capture/irradiance cube faces.
// We re-use the same vertex buffer for both sizes — UV is always 0..1,
// the screen-space coords match the face dimensions in the d3d viewport
// at draw time.
static RwIm2DVertex CubeQuad[4];
static RwImVertexIndex CubeIdx[6] = { 0, 1, 2, 0, 2, 3 };

static void
setupCubeQuad(float size)
{
	const float HALF = 0.5f;
	float hz = -HALF;
	float hxmax = size - HALF;
	float hymax = size - HALF;

	RwIm2DVertexSetScreenX(&CubeQuad[0], hz);
	RwIm2DVertexSetScreenY(&CubeQuad[0], hz);
	RwIm2DVertexSetScreenZ(&CubeQuad[0], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&CubeQuad[0], 1.0f);
	RwIm2DVertexSetRecipCameraZ(&CubeQuad[0], 1.0f);
	RwIm2DVertexSetU(&CubeQuad[0], 0.0f, 1.0f);
	RwIm2DVertexSetV(&CubeQuad[0], 0.0f, 1.0f);
	RwIm2DVertexSetIntRGBA(&CubeQuad[0], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&CubeQuad[1], hz);
	RwIm2DVertexSetScreenY(&CubeQuad[1], hymax);
	RwIm2DVertexSetScreenZ(&CubeQuad[1], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&CubeQuad[1], 1.0f);
	RwIm2DVertexSetRecipCameraZ(&CubeQuad[1], 1.0f);
	RwIm2DVertexSetU(&CubeQuad[1], 0.0f, 1.0f);
	RwIm2DVertexSetV(&CubeQuad[1], 1.0f, 1.0f);
	RwIm2DVertexSetIntRGBA(&CubeQuad[1], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&CubeQuad[2], hxmax);
	RwIm2DVertexSetScreenY(&CubeQuad[2], hymax);
	RwIm2DVertexSetScreenZ(&CubeQuad[2], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&CubeQuad[2], 1.0f);
	RwIm2DVertexSetRecipCameraZ(&CubeQuad[2], 1.0f);
	RwIm2DVertexSetU(&CubeQuad[2], 1.0f, 1.0f);
	RwIm2DVertexSetV(&CubeQuad[2], 1.0f, 1.0f);
	RwIm2DVertexSetIntRGBA(&CubeQuad[2], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&CubeQuad[3], hxmax);
	RwIm2DVertexSetScreenY(&CubeQuad[3], hz);
	RwIm2DVertexSetScreenZ(&CubeQuad[3], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&CubeQuad[3], 1.0f);
	RwIm2DVertexSetRecipCameraZ(&CubeQuad[3], 1.0f);
	RwIm2DVertexSetU(&CubeQuad[3], 1.0f, 1.0f);
	RwIm2DVertexSetV(&CubeQuad[3], 0.0f, 1.0f);
	RwIm2DVertexSetIntRGBA(&CubeQuad[3], 255, 255, 255, 255);
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
	if(!Enabled)
		return;

	int colorFmt = (int)rw::Raster::F16_RGBA;
	captureCube    = rw::d3d::createCubeTexture(CAPTURE_SIZE, colorFmt);
	irradianceCube = rw::d3d::createCubeTexture(IRRADIANCE_SIZE, colorFmt);

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

	// First-frame populate so the cube has content before the first
	// scene draw reads it.
	FrameCounter = REFRESH_PERIOD - 1;
	Update(cam);
#endif
}

void
CIBL::Close(void)
{
#ifdef RW_D3D9
	if(captureCube){ rw::d3d::destroyCubeTexture(captureCube); captureCube = nil; }
	if(irradianceCube){ rw::d3d::destroyCubeTexture(irradianceCube); irradianceCube = nil; }
	// Shaders stay loaded — they have no per-scene state and re-loading
	// them on every game-state transition would just churn.
#endif
}

#ifdef RW_D3D9
static void
renderCubeFace(void *dstCube, int face, float size, void *ps,
               const float *constsC10, int constCount)
{
	IDirect3DSurface9 *dstSurf = nil;
	((IDirect3DCubeTexture9*)dstCube)->GetCubeMapSurface((D3DCUBEMAP_FACES)face, 0, &dstSurf);
	if(dstSurf == nil) return;

	// Stash + swap RT slot 0. Skip MRT slots — those are gbuffer-only
	// and the cube render doesn't write to them.
	IDirect3DSurface9 *savedRT = nil;
	rw::d3d::d3ddevice->GetRenderTarget(0, &savedRT);
	rw::d3d::d3ddevice->SetRenderTarget(0, dstSurf);

	// Match the viewport to the face size.
	D3DVIEWPORT9 vp;
	vp.X = 0;
	vp.Y = 0;
	vp.Width  = (DWORD)size;
	vp.Height = (DWORD)size;
	vp.MinZ = 0.0f;
	vp.MaxZ = 1.0f;
	D3DVIEWPORT9 savedVp;
	rw::d3d::d3ddevice->GetViewport(&savedVp);
	rw::d3d::d3ddevice->SetViewport(&vp);

	// No depth / no blend.
	rw::d3d::d3ddevice->SetRenderState(D3DRS_ZENABLE, FALSE);
	rw::d3d::d3ddevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	rw::d3d::d3ddevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

	// Upload constants starting at c10.
	if(constsC10 && constCount > 0)
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, constsC10, constCount);

	setupCubeQuad(size);
	rw::d3d::im2dOverridePS = ps;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, CubeQuad, 4, CubeIdx, 6);
	rw::d3d::im2dOverridePS = nil;

	// Restore RT + viewport.
	rw::d3d::d3ddevice->SetRenderTarget(0, savedRT);
	rw::d3d::d3ddevice->SetViewport(&savedVp);
	if(savedRT) savedRT->Release();
	dstSurf->Release();
	rw::d3d::d3ddevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}
#endif

void
CIBL::Update(RwCamera *cam)
{
	(void)cam;
#ifdef RW_D3D9
	if(!Enabled || captureCube == nil || irradianceCube == nil)
		return;
	if(iblSkyToCube_PS == nil || iblConvolve_PS == nil)
		return;

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
		renderCubeFace(captureCube, face, (float)CAPTURE_SIZE,
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
		renderCubeFace(irradianceCube, face, (float)IRRADIANCE_SIZE,
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
#endif
}

void
CIBL::UnbindReceiver(void)
{
#ifdef RW_D3D9
	rw::d3d::bindCubeToSampler(7, nil);
#endif
}

#endif
