#define WITHD3D
#include "common.h"

#ifdef EXTENDED_COLOURFILTER

#ifndef LIBRW
#error "Need librw for EXTENDED_COLOURFILTER"
#endif

#include "main.h"
#include "RwHelper.h"
#include "Camera.h"
#include "MBlur.h"
#include "Timecycle.h"
#include "Weather.h"	// CWeather::LightningFlash, Rain, WetRoads
#include "WaterLevel.h"	// CWaterLevel::GetWaterLevelNoWaves (underwater fog detection)
#include "Lights.h"	// pDirect (sun light) + DirectionalLightColourForFrame
#include "postfx.h"
#ifdef POSTFX_HDR
#include "gbuffer.h"
extern RwRGBAReal DirectionalLightColourForFrame;
#endif
#ifdef POSTFX_WATER_REFLECTION
#include "waterReflection.h"
#endif
#ifdef POSTFX_CSM
#include "csm.h"
#endif

RwRaster *CPostFX::pFrontBuffer;
RwRaster *CPostFX::pBackBuffer;
bool CPostFX::bJustInitialised;
int CPostFX::EffectSwitch = POSTFX_NORMAL;
bool CPostFX::BlurOn = false;
bool CPostFX::MotionBlurOn = false;

#ifdef POSTFX_BLOOM
RwRaster *CPostFX::pBloomA;
RwRaster *CPostFX::pBloomB;
bool CPostFX::BloomEnable = true;
// Conservative defaults — only the brightest pixels bloom, and the
// composite stays subtle so the scene doesn't look hazy.
float CPostFX::BloomThreshold = 0.95f;
float CPostFX::BloomKnee = 0.15f;
float CPostFX::BloomIntensity = 0.28f;
float CPostFX::BloomSaturation = 1.0f;
#endif
#ifdef POSTFX_FXAA
bool CPostFX::FxaaEnable = true;
float CPostFX::FxaaStrength = 0.75f;
#endif
#ifdef POSTFX_GODRAYS
// Off by default — looks great at sunrise/sunset but is too obvious
// midday. Players can enable it via the menu.
bool CPostFX::GodRaysEnable = false;
float CPostFX::GodRaysDensity = 0.95f;
float CPostFX::GodRaysDecay = 0.965f;
float CPostFX::GodRaysExposure = 0.45f;
float CPostFX::GodRaysWeight = 0.45f;
#endif
#ifdef POSTFX_TONEMAP
// ACES expects linear HDR input; running it on the existing sRGB LDR
// scene desaturates the picture and clips shadows. Default OFF — players
// can opt in once we have a true HDR backbuffer.
bool CPostFX::TonemapACES = false;
bool CPostFX::TonemapGamma = false;
float CPostFX::Exposure = 1.0f;
float CPostFX::Saturation = 1.0f;	// neutral
float CPostFX::VignetteIntensity = 0.0f;
float CPostFX::VignetteSoftness = 0.45f;
float CPostFX::VignetteRoundness = 1.0f;
float CPostFX::CAStrength = 0.0f;
float CPostFX::CADistanceScale = 1.0f;
#endif
#ifdef POSTFX_HDR
RwRaster *CPostFX::pSsaoA;
RwRaster *CPostFX::pSsaoB;
bool CPostFX::SsaoEnable = true;
// Conservative defaults — tightened from the original 0.9 / 1.8 / 0.85 set
// to suppress the dark halo around pedestrians that the player reported.
// The G-buffer clear fix in CGBuffer::BeginScenePass kills the bulk of the
// SSAO artefacts (sky / water smear) but a smaller sampling radius keeps
// edge occlusion subtler.
float CPostFX::SsaoRadius = 0.55f;
float CPostFX::SsaoBias = 0.03f;
float CPostFX::SsaoIntensity = 1.2f;
float CPostFX::SsaoStrength = 0.6f;
float CPostFX::SsaoPower = 1.4f;
int CPostFX::SsaoAlgorithm = 0;	// default to classic SSAO; GTAO is opt-in
// Procedural IBL — defaults that feel like a "global gradient" ambient.
// Off by default until the player opts in via the menu; settings.ini
// remembers the choice.
bool CPostFX::IblEnabled = false;
float CPostFX::IblIntensity = 0.55f;
float CPostFX::IblHorizonExp = 2.2f;
float CPostFX::IblExposure = 1.0f / 255.0f;	// CTimeCycle gives 0..255 bytes
float CPostFX::IblGroundTint = 0.6f;
// Wet surfaces — drives the rain-soaked look. Defaults map the engine's
// 0..1 WetRoads signal directly to the shader.
bool CPostFX::WetSurfacesEnable = true;
float CPostFX::WetSurfacesIntensity = 1.0f;
float CPostFX::WetSurfacesDiffuse = 0.45f;	// wet asphalt ~half as bright
float CPostFX::WetSurfacesSpec = 3.2f;	// strong sheen
float CPostFX::WetSurfacesPower = 2.5f;	// tighter highlight when wet
// Contact AO defaults — subtle by default; the player can crank it via
// the menu if they want sharper foot/tyre/door contacts.
float CPostFX::SsaoContactStrength = 0.35f;
float CPostFX::SsaoContactRadius = 3.5f;	// ~3-4 pixels at 1080p
float CPostFX::SsaoContactMaxDz = 0.6f;	// 60 cm window — bigger gaps are not contact
// Contact shadows — moderate by default; visible improvement around
// foliage, doorframes, building corners against the sun.
float CPostFX::ContactShadowStrength = 0.5f;
int CPostFX::ContactShadowSteps = 10;
float CPostFX::ContactShadowThickness = 1.2f;
float CPostFX::ContactShadowBias = 0.05f;

// Screen-Space Reflections — off by default until the player opts in.
RwRaster *CPostFX::pSsrA;
bool CPostFX::SsrEnable = false;
float CPostFX::SsrMaxDistance = 30.0f;
int CPostFX::SsrStepCount = 18;
float CPostFX::SsrThickness = 0.5f;
float CPostFX::SsrStrength = 0.6f;
float CPostFX::SsrFresnelBias = 0.04f;
// Depth of field — off by default; defaults give a tasteful cinematic
// near/far blur centred on ~15m (typical car interior distance).
RwRaster *CPostFX::pDofScratch;
bool CPostFX::DofEnable = false;
float CPostFX::DofFocusDistance = 15.0f;
float CPostFX::DofFocusRange = 6.0f;
float CPostFX::DofAperture = 0.012f;
// Volumetric fog — defaults tuned for Vice City's daytime haze look.
// Density is modest so the scene doesn't read as foggy; the in-scatter is
// what gives the warm "filled" feel toward the sun. Disabled by default
// until the menu toggle is wired (so existing saves don't suddenly fog).
bool CPostFX::VolFogEnable = false;
float CPostFX::VolFogStrength = 0.8f;
float CPostFX::VolFogDensity = 0.015f;
float CPostFX::VolFogHeightFalloff = 0.018f;	// fog ~halves every ~38m up
float CPostFX::VolFogGroundZ = -10.0f;	// VC ground is around z=0..20; -10 gives some slack
float CPostFX::VolFogMaxDist = 350.0f;
float CPostFX::VolFogHG = 0.55f;
float CPostFX::VolFogSunBoost = 1.0f;
RwRaster *CPostFX::pTaaHistA;
RwRaster *CPostFX::pTaaHistB;
bool CPostFX::TaaEnable = false;	// opt-in (FXAA stays default)
float CPostFX::TaaBlend = 0.12f;
float CPostFX::TaaClamp = 1.0f;
int CPostFX::TaaFrameIdx = 0;
#endif

static RwIm2DVertex Vertex[4];
static RwIm2DVertex Vertex2[4];
static RwImVertexIndex Index[6] = { 0, 1, 2, 0, 2, 3 };
static int32 g_postfxRtWidth, g_postfxRtHeight;
#ifdef POSTFX_HDR
// Dedicated quad sized to the camera resolution (not the pow2 RT size used
// by pBackBuffer). pHdrScene is exactly camera-sized, so we need the
// UV=0..1 to map to the camera-sized backbuffer 1:1, otherwise the resolve
// pass stretches the HDR image (visible as a wider FOV with HDR on).
static RwIm2DVertex HdrResolveVertex[4];
static int32 g_hdrResolveW, g_hdrResolveH;
#endif

#ifdef RW_D3D9
void *colourfilterVC_PS;
void *contrast_PS;
#ifdef POSTFX_BLOOM
static void *brightpass_PS;
static void *bloomBlur_PS;
static void *bloomComposite_PS;
#endif
#ifdef POSTFX_FXAA
static void *fxaa_PS;
#endif
#ifdef POSTFX_GODRAYS
static void *godrays_PS;
#endif
#ifdef POSTFX_HDR
void *hdrResolve_PS;
static void *ssao_PS;
static void *ssaoBlur_PS;
static void *gtao_PS;	// alternative AO algorithm (CPostFX::SsaoAlgorithm = 1)
static rw::Camera *ssaoCamA;
static rw::Camera *ssaoCamB;
static RwIm2DVertex SsaoVertex[4];	// half-res quad sized to SSAO RT
static int32 g_ssaoW, g_ssaoH;
static void *taa_PS;
static rw::Camera *taaCamA;
static rw::Camera *taaCamB;
// Cached at the end of each RenderTAA invocation so the next frame's
// reprojection knows where the world points used to land on screen.
// Initialised to identity; the first frame falls back to no-motion
// (same UV) which matches the legacy camera-only behaviour.
static rw::RawMatrix taaPrevViewProj = {
	{ 1, 0, 0 }, 0,
	{ 0, 1, 0 }, 0,
	{ 0, 0, 1 }, 0,
	{ 0, 0, 0 }, 1,
};
static void *ssr_PS;
static rw::Camera *ssrCam;	// half-res RGBA8 SSR target
static void *dof_PS;
static rw::Camera *dofCam;	// full-res RGBA16F bokeh scratch
#endif
#ifdef SOFT_SHADOWS
void *shadowPCF_PS;
float shadowPCFTexelSize[4] = { 1.0f/256.0f, 1.0f/256.0f, 1.6f, 0.0f };
float shadowPCFRadius = 1.6f;
#endif
#endif

#ifdef SOFT_SHADOWS
void
EnableShadowPCF(int textureSize)
{
#ifdef RW_D3D9
	if(shadowPCF_PS == nil)
		return;
	float invSize = (textureSize > 0) ? 1.0f / (float)textureSize : 1.0f / 128.0f;
	shadowPCFTexelSize[0] = invSize;
	shadowPCFTexelSize[1] = invSize;
	shadowPCFTexelSize[2] = shadowPCFRadius;
	shadowPCFTexelSize[3] = 0.0f;
	rw::d3d::d3ddevice->SetPixelShaderConstantF(10, shadowPCFTexelSize, 1);
	rw::d3d::im3dOverridePS = shadowPCF_PS;
#endif
}

void
DisableShadowPCF(void)
{
#ifdef RW_D3D9
	rw::d3d::im3dOverridePS = nil;
#endif
}
#endif

#ifdef POSTFX_BLOOM
static rw::Camera *bloomCamA;
static rw::Camera *bloomCamB;

static rw::Camera*
CreateBloomCam(rw::Raster *fbuf)
{
	rw::Frame *frame = rw::Frame::create();
	if(frame == nil) return nil;
	rw::Camera *cam = rw::Camera::create();
	if(cam == nil){ frame->destroy(); return nil; }
	cam->frameBuffer = fbuf;
	cam->zBuffer = nil;
	cam->setFrame(frame);
	cam->setNearPlane(0.1f);
	cam->setFarPlane(1000.0f);
	rw::V2d vw = { 1.0f, 1.0f };
	cam->setViewWindow(&vw);
	return cam;
}

static void
DestroyBloomCam(rw::Camera *cam)
{
	if(cam == nil) return;
	cam->frameBuffer = nil; // we own the raster elsewhere
	cam->zBuffer = nil;
	rw::Frame *f = cam->getFrame();
	if(f){
		cam->setFrame(nil);
		f->destroy();
	}
	cam->destroy();
}
#endif
#ifdef RW_OPENGL
int32 u_blurcolor;
int32 u_contrastAdd;
int32 u_contrastMult;
rw::gl3::Shader *colourFilterVC;
rw::gl3::Shader *contrast;
#endif

void
CPostFX::InitOnce(void)
{
#ifdef RW_OPENGL
	u_blurcolor = rw::gl3::registerUniform("u_blurcolor");
	u_contrastAdd = rw::gl3::registerUniform("u_contrastAdd");
	u_contrastMult = rw::gl3::registerUniform("u_contrastMult");
#endif
}

void
CPostFX::Open(RwCamera *cam)
{
	if(pFrontBuffer)
		Close();

	uint32 width  = Pow(2.0f, int32(log2(RwRasterGetWidth (RwCameraGetRaster(cam))))+1);
	uint32 height = Pow(2.0f, int32(log2(RwRasterGetHeight(RwCameraGetRaster(cam))))+1);
	uint32 depth  = RwRasterGetDepth(RwCameraGetRaster(cam));
	pFrontBuffer = RwRasterCreate(width, height, depth, rwRASTERTYPECAMERATEXTURE);
	pBackBuffer = RwRasterCreate(width, height, depth, rwRASTERTYPECAMERATEXTURE);
#ifdef POSTFX_BLOOM
	pBloomA = RwRasterCreate(width, height, depth, rwRASTERTYPECAMERATEXTURE);
	pBloomB = RwRasterCreate(width, height, depth, rwRASTERTYPECAMERATEXTURE);
	bloomCamA = CreateBloomCam(pBloomA);
	bloomCamB = CreateBloomCam(pBloomB);
#endif
#ifdef POSTFX_HDR
	// HDR scene RT + G-buffer share the camera's actual resolution
	// (non-pow2 is fine on ps_3_0; tonemap-resolve samples UV [0,1]).
	CGBuffer::Open(cam);
#ifdef POSTFX_WATER_REFLECTION
	CWaterReflection::Open(cam);
#endif
#ifdef POSTFX_CSM
	CCSM::Open(cam);
#endif

	// TAA history buffers — full pow2 size (same as pBackBuffer) so we can
	// reuse the existing Vertex[] quad for the blend pass.
	pTaaHistA = RwRasterCreate(width, height, depth, rwRASTERTYPECAMERATEXTURE);
	pTaaHistB = RwRasterCreate(width, height, depth, rwRASTERTYPECAMERATEXTURE);
	taaCamA = CreateBloomCam(pTaaHistA);
	taaCamB = CreateBloomCam(pTaaHistB);
	TaaFrameIdx = 0;

	// Half-res SSAO ping-pong RTs (RGBA8, R channel = AO; alpha ignored).
	// Half-res is the standard SSAO economy trade-off — ~4x cheaper than
	// full-res with no visible quality loss after bilateral blur.
	int32 cw = RwRasterGetWidth(RwCameraGetRaster(cam));
	int32 ch = RwRasterGetHeight(RwCameraGetRaster(cam));
	int32 sw = cw / 2;
	int32 sh = ch / 2;
	if(sw < 64) sw = 64;
	if(sh < 64) sh = 64;
	pSsaoA = RwRasterCreate(sw, sh, depth, rwRASTERTYPECAMERATEXTURE);
	pSsaoB = RwRasterCreate(sw, sh, depth, rwRASTERTYPECAMERATEXTURE);
	ssaoCamA = CreateBloomCam(pSsaoA);
	ssaoCamB = CreateBloomCam(pSsaoB);
	g_ssaoW = sw;
	g_ssaoH = sh;

	// SSR shares the half-res target size with SSAO. RGBA8 is enough — the
	// reflection signal is LDR-ish (it's already gone through the same path
	// hdrResolve does) so we tonemap before storing. Camera reuses the
	// existing helper so SSR draws into a real librw camera bound to the
	// raster.
	pSsrA = RwRasterCreate(sw, sh, depth, rwRASTERTYPECAMERATEXTURE);
	ssrCam = CreateBloomCam(pSsrA);

	// DoF scratch — full-res RGBA16F so the bokeh blur preserves HDR
	// brightness for the tonemap that follows. Same dimensions as
	// pHdrScene; we ping-pong (pHdrScene → pDofScratch → pHdrScene
	// via copy or rebind) inside RenderDoF.
	{
		int32 colorFmt = (int32)rw::Raster::CAMERATEXTURE | (int32)rw::Raster::F16_RGBA;
		pDofScratch = RwRasterCreate(cw, ch, 0, colorFmt);
		dofCam = CreateBloomCam(pDofScratch);
	}

	// Half-res quad for SSAO passes. The destination RTs are exactly sw x sh
	// (non-pow2), so UV=0..1 must map to (0, 0)..(sw, sh) screen coords.
	{
		float hz, hxmax, hymax;
		if(depth == 16){
			hz = HALFPX;
			hxmax = (float)sw + HALFPX;
			hymax = (float)sh + HALFPX;
		}else{
			hz = -HALFPX;
			hxmax = (float)sw - HALFPX;
			hymax = (float)sh - HALFPX;
		}
		float invNear = 1.0f / RwCameraGetNearClipPlane(cam);
		RwIm2DVertexSetScreenX(&SsaoVertex[0], hz);
		RwIm2DVertexSetScreenY(&SsaoVertex[0], hz);
		RwIm2DVertexSetScreenZ(&SsaoVertex[0], RwIm2DGetNearScreenZ());
		RwIm2DVertexSetCameraZ(&SsaoVertex[0], RwCameraGetNearClipPlane(cam));
		RwIm2DVertexSetRecipCameraZ(&SsaoVertex[0], invNear);
		RwIm2DVertexSetU(&SsaoVertex[0], 0.0f, invNear);
		RwIm2DVertexSetV(&SsaoVertex[0], 0.0f, invNear);
		RwIm2DVertexSetIntRGBA(&SsaoVertex[0], 255, 255, 255, 255);

		RwIm2DVertexSetScreenX(&SsaoVertex[1], hz);
		RwIm2DVertexSetScreenY(&SsaoVertex[1], hymax);
		RwIm2DVertexSetScreenZ(&SsaoVertex[1], RwIm2DGetNearScreenZ());
		RwIm2DVertexSetCameraZ(&SsaoVertex[1], RwCameraGetNearClipPlane(cam));
		RwIm2DVertexSetRecipCameraZ(&SsaoVertex[1], invNear);
		RwIm2DVertexSetU(&SsaoVertex[1], 0.0f, invNear);
		RwIm2DVertexSetV(&SsaoVertex[1], 1.0f, invNear);
		RwIm2DVertexSetIntRGBA(&SsaoVertex[1], 255, 255, 255, 255);

		RwIm2DVertexSetScreenX(&SsaoVertex[2], hxmax);
		RwIm2DVertexSetScreenY(&SsaoVertex[2], hymax);
		RwIm2DVertexSetScreenZ(&SsaoVertex[2], RwIm2DGetNearScreenZ());
		RwIm2DVertexSetCameraZ(&SsaoVertex[2], RwCameraGetNearClipPlane(cam));
		RwIm2DVertexSetRecipCameraZ(&SsaoVertex[2], invNear);
		RwIm2DVertexSetU(&SsaoVertex[2], 1.0f, invNear);
		RwIm2DVertexSetV(&SsaoVertex[2], 1.0f, invNear);
		RwIm2DVertexSetIntRGBA(&SsaoVertex[2], 255, 255, 255, 255);

		RwIm2DVertexSetScreenX(&SsaoVertex[3], hxmax);
		RwIm2DVertexSetScreenY(&SsaoVertex[3], hz);
		RwIm2DVertexSetScreenZ(&SsaoVertex[3], RwIm2DGetNearScreenZ());
		RwIm2DVertexSetCameraZ(&SsaoVertex[3], RwCameraGetNearClipPlane(cam));
		RwIm2DVertexSetRecipCameraZ(&SsaoVertex[3], invNear);
		RwIm2DVertexSetU(&SsaoVertex[3], 1.0f, invNear);
		RwIm2DVertexSetV(&SsaoVertex[3], 0.0f, invNear);
		RwIm2DVertexSetIntRGBA(&SsaoVertex[3], 255, 255, 255, 255);
	}
#endif
	g_postfxRtWidth = width;
	g_postfxRtHeight = height;
	bJustInitialised = true;

	float zero, xmax, ymax;

	if(RwRasterGetDepth(RwCameraGetRaster(cam)) == 16){
		zero = HALFPX;
		xmax = width + HALFPX;
		ymax = height + HALFPX;
	}else{
		zero = -HALFPX;
		xmax = width - HALFPX;
		ymax = height - HALFPX;
	}

	RwIm2DVertexSetScreenX(&Vertex[0], zero);
	RwIm2DVertexSetScreenY(&Vertex[0], zero);
	RwIm2DVertexSetScreenZ(&Vertex[0], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex[0], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex[0], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex[0], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex[0], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex[0], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex[1], zero);
	RwIm2DVertexSetScreenY(&Vertex[1], ymax);
	RwIm2DVertexSetScreenZ(&Vertex[1], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex[1], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex[1], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex[1], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex[1], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex[1], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex[2], xmax);
	RwIm2DVertexSetScreenY(&Vertex[2], ymax);
	RwIm2DVertexSetScreenZ(&Vertex[2], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex[2], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex[2], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex[2], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex[2], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex[2], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex[3], xmax);
	RwIm2DVertexSetScreenY(&Vertex[3], zero);
	RwIm2DVertexSetScreenZ(&Vertex[3], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex[3], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex[3], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex[3], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex[3], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex[3], 255, 255, 255, 255);


	RwIm2DVertexSetScreenX(&Vertex2[0], zero + 2.0f);
	RwIm2DVertexSetScreenY(&Vertex2[0], zero + 2.0f);
	RwIm2DVertexSetScreenZ(&Vertex2[0], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex2[0], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex2[0], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex2[0], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex2[0], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex2[0], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex2[1], 2.0f);
	RwIm2DVertexSetScreenY(&Vertex2[1], ymax + 2.0f);
	RwIm2DVertexSetScreenZ(&Vertex2[1], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex2[1], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex2[1], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex2[1], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex2[1], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex2[1], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex2[2], xmax + 2.0f);
	RwIm2DVertexSetScreenY(&Vertex2[2], ymax + 2.0f);
	RwIm2DVertexSetScreenZ(&Vertex2[2], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex2[2], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex2[2], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex2[2], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex2[2], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex2[2], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Vertex2[3], xmax + 2.0f);
	RwIm2DVertexSetScreenY(&Vertex2[3], zero + 2.0f);
	RwIm2DVertexSetScreenZ(&Vertex2[3], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Vertex2[3], RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetRecipCameraZ(&Vertex2[3], 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetU(&Vertex2[3], 1.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetV(&Vertex2[3], 0.0f, 1.0f/RwCameraGetNearClipPlane(cam));
	RwIm2DVertexSetIntRGBA(&Vertex2[3], 255, 255, 255, 255);

#ifdef POSTFX_HDR
	// HDR-resolve quad. Sized to the actual camera resolution so a UV=0..1
	// over the quad maps 1:1 onto the camera-sized pHdrScene texture.
	{
		float cw = (float)RwRasterGetWidth(RwCameraGetRaster(cam));
		float ch = (float)RwRasterGetHeight(RwCameraGetRaster(cam));
		float hz, hxmax, hymax;
		if(RwRasterGetDepth(RwCameraGetRaster(cam)) == 16){
			hz = HALFPX;
			hxmax = cw + HALFPX;
			hymax = ch + HALFPX;
		}else{
			hz = -HALFPX;
			hxmax = cw - HALFPX;
			hymax = ch - HALFPX;
		}
		float invNear = 1.0f / RwCameraGetNearClipPlane(cam);

		RwIm2DVertexSetScreenX(&HdrResolveVertex[0], hz);
		RwIm2DVertexSetScreenY(&HdrResolveVertex[0], hz);
		RwIm2DVertexSetScreenZ(&HdrResolveVertex[0], RwIm2DGetNearScreenZ());
		RwIm2DVertexSetCameraZ(&HdrResolveVertex[0], RwCameraGetNearClipPlane(cam));
		RwIm2DVertexSetRecipCameraZ(&HdrResolveVertex[0], invNear);
		RwIm2DVertexSetU(&HdrResolveVertex[0], 0.0f, invNear);
		RwIm2DVertexSetV(&HdrResolveVertex[0], 0.0f, invNear);
		RwIm2DVertexSetIntRGBA(&HdrResolveVertex[0], 255, 255, 255, 255);

		RwIm2DVertexSetScreenX(&HdrResolveVertex[1], hz);
		RwIm2DVertexSetScreenY(&HdrResolveVertex[1], hymax);
		RwIm2DVertexSetScreenZ(&HdrResolveVertex[1], RwIm2DGetNearScreenZ());
		RwIm2DVertexSetCameraZ(&HdrResolveVertex[1], RwCameraGetNearClipPlane(cam));
		RwIm2DVertexSetRecipCameraZ(&HdrResolveVertex[1], invNear);
		RwIm2DVertexSetU(&HdrResolveVertex[1], 0.0f, invNear);
		RwIm2DVertexSetV(&HdrResolveVertex[1], 1.0f, invNear);
		RwIm2DVertexSetIntRGBA(&HdrResolveVertex[1], 255, 255, 255, 255);

		RwIm2DVertexSetScreenX(&HdrResolveVertex[2], hxmax);
		RwIm2DVertexSetScreenY(&HdrResolveVertex[2], hymax);
		RwIm2DVertexSetScreenZ(&HdrResolveVertex[2], RwIm2DGetNearScreenZ());
		RwIm2DVertexSetCameraZ(&HdrResolveVertex[2], RwCameraGetNearClipPlane(cam));
		RwIm2DVertexSetRecipCameraZ(&HdrResolveVertex[2], invNear);
		RwIm2DVertexSetU(&HdrResolveVertex[2], 1.0f, invNear);
		RwIm2DVertexSetV(&HdrResolveVertex[2], 1.0f, invNear);
		RwIm2DVertexSetIntRGBA(&HdrResolveVertex[2], 255, 255, 255, 255);

		RwIm2DVertexSetScreenX(&HdrResolveVertex[3], hxmax);
		RwIm2DVertexSetScreenY(&HdrResolveVertex[3], hz);
		RwIm2DVertexSetScreenZ(&HdrResolveVertex[3], RwIm2DGetNearScreenZ());
		RwIm2DVertexSetCameraZ(&HdrResolveVertex[3], RwCameraGetNearClipPlane(cam));
		RwIm2DVertexSetRecipCameraZ(&HdrResolveVertex[3], invNear);
		RwIm2DVertexSetU(&HdrResolveVertex[3], 1.0f, invNear);
		RwIm2DVertexSetV(&HdrResolveVertex[3], 0.0f, invNear);
		RwIm2DVertexSetIntRGBA(&HdrResolveVertex[3], 255, 255, 255, 255);

		g_hdrResolveW = (int32)cw;
		g_hdrResolveH = (int32)ch;
	}
#endif


#ifdef RW_D3D9
#include "shaders/obj/colourfilterVC_PS.inc"
	colourfilterVC_PS = rw::d3d::createPixelShader(colourfilterVC_PS_cso);
#include "shaders/obj/contrastPS.inc"
	contrast_PS = rw::d3d::createPixelShader(contrastPS_cso);
#ifdef POSTFX_BLOOM
	{
#include "shaders/obj/brightpass_PS.inc"
	brightpass_PS = rw::d3d::createPixelShader(brightpass_PS_cso);
	}
	{
#include "shaders/obj/bloomBlur_PS.inc"
	bloomBlur_PS = rw::d3d::createPixelShader(bloomBlur_PS_cso);
	}
	{
#include "shaders/obj/bloomComposite_PS.inc"
	bloomComposite_PS = rw::d3d::createPixelShader(bloomComposite_PS_cso);
	}
#endif
#ifdef POSTFX_FXAA
	{
#include "shaders/obj/fxaa_PS.inc"
	fxaa_PS = rw::d3d::createPixelShader(fxaa_PS_cso);
	}
#endif
#ifdef POSTFX_GODRAYS
	{
#include "shaders/obj/godrays_PS.inc"
	godrays_PS = rw::d3d::createPixelShader(godrays_PS_cso);
	}
#endif
#ifdef POSTFX_HDR
	{
#include "shaders/obj/hdrResolve_PS.inc"
	hdrResolve_PS = rw::d3d::createPixelShader(hdrResolve_PS_cso);
	}
	{
#include "shaders/obj/ssao_PS.inc"
	ssao_PS = rw::d3d::createPixelShader(ssao_PS_cso);
	}
	{
#include "shaders/obj/ssaoBlur_PS.inc"
	ssaoBlur_PS = rw::d3d::createPixelShader(ssaoBlur_PS_cso);
	}
	{
#include "shaders/obj/taa_PS.inc"
	taa_PS = rw::d3d::createPixelShader(taa_PS_cso);
	}
	{
#include "shaders/obj/ssr_PS.inc"
	ssr_PS = rw::d3d::createPixelShader(ssr_PS_cso);
	}
	{
#include "shaders/obj/dof_PS.inc"
	dof_PS = rw::d3d::createPixelShader(dof_PS_cso);
	}
	{
#include "shaders/obj/gtao_PS.inc"
	gtao_PS = rw::d3d::createPixelShader(gtao_PS_cso);
	}
#endif
#ifdef SOFT_SHADOWS
	{
#include "shaders/obj/shadowPCF_PS.inc"
	shadowPCF_PS = rw::d3d::createPixelShader(shadowPCF_PS_cso);
	}
#endif
#endif
#ifdef RW_OPENGL
	using namespace rw::gl3;

	{
#include "shaders/obj/im2d_vert.inc"
#include "shaders/obj/colourfilterVC_frag.inc"
	const char *vs[] = { shaderDecl, header_vert_src, im2d_vert_src, nil };
	const char *fs[] = { shaderDecl, header_frag_src, colourfilterVC_frag_src, nil };
	colourFilterVC = Shader::create(vs, fs);
	assert(colourFilterVC);
	}

	{
#include "shaders/obj/im2d_vert.inc"
#include "shaders/obj/contrast_frag.inc"
	const char *vs[] = { shaderDecl, header_vert_src, im2d_vert_src, nil };
	const char *fs[] = { shaderDecl, header_frag_src, contrast_frag_src, nil };
	contrast = Shader::create(vs, fs);
	assert(contrast);
	}

#endif
}

void
CPostFX::Close(void)
{
	if(pFrontBuffer){
		RwRasterDestroy(pFrontBuffer);
		pFrontBuffer = nil;
	}
	if(pBackBuffer){
		RwRasterDestroy(pBackBuffer);
		pBackBuffer = nil;
	}
#ifdef POSTFX_BLOOM
	if(bloomCamA){ DestroyBloomCam(bloomCamA); bloomCamA = nil; }
	if(bloomCamB){ DestroyBloomCam(bloomCamB); bloomCamB = nil; }
	if(pBloomA){ RwRasterDestroy(pBloomA); pBloomA = nil; }
	if(pBloomB){ RwRasterDestroy(pBloomB); pBloomB = nil; }
#endif
#ifdef POSTFX_HDR
	if(ssaoCamA){ DestroyBloomCam(ssaoCamA); ssaoCamA = nil; }
	if(ssaoCamB){ DestroyBloomCam(ssaoCamB); ssaoCamB = nil; }
	if(pSsaoA){ RwRasterDestroy(pSsaoA); pSsaoA = nil; }
	if(pSsaoB){ RwRasterDestroy(pSsaoB); pSsaoB = nil; }
	if(ssrCam){ DestroyBloomCam(ssrCam); ssrCam = nil; }
	if(pSsrA){ RwRasterDestroy(pSsrA); pSsrA = nil; }
	if(dofCam){ DestroyBloomCam(dofCam); dofCam = nil; }
	if(pDofScratch){ RwRasterDestroy(pDofScratch); pDofScratch = nil; }
	if(taaCamA){ DestroyBloomCam(taaCamA); taaCamA = nil; }
	if(taaCamB){ DestroyBloomCam(taaCamB); taaCamB = nil; }
	if(pTaaHistA){ RwRasterDestroy(pTaaHistA); pTaaHistA = nil; }
	if(pTaaHistB){ RwRasterDestroy(pTaaHistB); pTaaHistB = nil; }
#ifdef POSTFX_WATER_REFLECTION
	CWaterReflection::Close();
#endif
#ifdef POSTFX_CSM
	CCSM::Close();
#endif
	CGBuffer::Close();
#endif
#ifdef RW_D3D9
	if(colourfilterVC_PS){
		rw::d3d::destroyPixelShader(colourfilterVC_PS);
		colourfilterVC_PS = nil;
	}
	if(contrast_PS){
		rw::d3d::destroyPixelShader(contrast_PS);
		contrast_PS = nil;
	}
#ifdef POSTFX_BLOOM
	if(brightpass_PS){ rw::d3d::destroyPixelShader(brightpass_PS); brightpass_PS = nil; }
	if(bloomBlur_PS){ rw::d3d::destroyPixelShader(bloomBlur_PS); bloomBlur_PS = nil; }
	if(bloomComposite_PS){ rw::d3d::destroyPixelShader(bloomComposite_PS); bloomComposite_PS = nil; }
#endif
#ifdef POSTFX_FXAA
	if(fxaa_PS){ rw::d3d::destroyPixelShader(fxaa_PS); fxaa_PS = nil; }
#endif
#ifdef POSTFX_GODRAYS
	if(godrays_PS){ rw::d3d::destroyPixelShader(godrays_PS); godrays_PS = nil; }
#endif
#ifdef POSTFX_HDR
	if(hdrResolve_PS){ rw::d3d::destroyPixelShader(hdrResolve_PS); hdrResolve_PS = nil; }
	if(ssao_PS){ rw::d3d::destroyPixelShader(ssao_PS); ssao_PS = nil; }
	if(ssaoBlur_PS){ rw::d3d::destroyPixelShader(ssaoBlur_PS); ssaoBlur_PS = nil; }
	if(taa_PS){ rw::d3d::destroyPixelShader(taa_PS); taa_PS = nil; }
	if(ssr_PS){ rw::d3d::destroyPixelShader(ssr_PS); ssr_PS = nil; }
	if(dof_PS){ rw::d3d::destroyPixelShader(dof_PS); dof_PS = nil; }
	if(gtao_PS){ rw::d3d::destroyPixelShader(gtao_PS); gtao_PS = nil; }
#endif
#ifdef SOFT_SHADOWS
	if(shadowPCF_PS){ rw::d3d::destroyPixelShader(shadowPCF_PS); shadowPCF_PS = nil; }
#endif
#endif
#ifdef RW_OPENGL
	if(colourFilterVC){
		colourFilterVC->destroy();
		colourFilterVC = nil;
	}
	if(contrast){
		contrast->destroy();
		contrast = nil;
	}
#endif
}

void
CPostFX::RenderOverlayBlur(RwCamera *cam, int32 r, int32 g, int32 b, int32 a)
{
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);

	RwIm2DVertexSetIntRGBA(&Vertex[0], r*2, g*2, b*2, 30);
	RwIm2DVertexSetIntRGBA(&Vertex[1], r*2, g*2, b*2, 30);
	RwIm2DVertexSetIntRGBA(&Vertex[2], r*2, g*2, b*2, 30);
	RwIm2DVertexSetIntRGBA(&Vertex[3], r*2, g*2, b*2, 30);
	RwIm2DVertexSetIntRGBA(&Vertex2[0], r*2, g*2, b*2, 30);
	RwIm2DVertexSetIntRGBA(&Vertex2[1], r*2, g*2, b*2, 30);
	RwIm2DVertexSetIntRGBA(&Vertex2[2], r*2, g*2, b*2, 30);
	RwIm2DVertexSetIntRGBA(&Vertex2[3], r*2, g*2, b*2, 30);

	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, BlurOn ? Vertex2 : Vertex, 4, Index, 6);


	RwIm2DVertexSetIntRGBA(&Vertex2[0], r, g, b, a);
	RwIm2DVertexSetIntRGBA(&Vertex[0], r, g, b, a);
	RwIm2DVertexSetIntRGBA(&Vertex2[1], r, g, b, a);
	RwIm2DVertexSetIntRGBA(&Vertex[1], r, g, b, a);
	RwIm2DVertexSetIntRGBA(&Vertex2[2], r, g, b, a);
	RwIm2DVertexSetIntRGBA(&Vertex[2], r, g, b, a);
	RwIm2DVertexSetIntRGBA(&Vertex2[3], r, g, b, a);
	RwIm2DVertexSetIntRGBA(&Vertex[3], r, g, b, a);

	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, BlurOn ? Vertex2 : Vertex, 4, Index, 6);
}

void
CPostFX::RenderOverlaySniper(RwCamera *cam, int32 r, int32 g, int32 b, int32 a)
{
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);

	RwIm2DVertexSetIntRGBA(&Vertex[0], r, g, b, 80);
	RwIm2DVertexSetIntRGBA(&Vertex[1], r, g, b, 80);
	RwIm2DVertexSetIntRGBA(&Vertex[2], r, g, b, 80);
	RwIm2DVertexSetIntRGBA(&Vertex[3], r, g, b, 80);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
}

float CPostFX::Intensity = 1.0f;

void
CPostFX::RenderOverlayShader(RwCamera *cam, int32 r, int32 g, int32 b, int32 a)
{
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBackBuffer);

	if(EffectSwitch == POSTFX_MOBILE){
		float mult[3], add[3];
		mult[0] = (r-64)/256.0f + 1.4f;
		mult[1] = (g-64)/256.0f + 1.4f;
		mult[2] = (b-64)/256.0f + 1.4f;
		add[0] = r/1536.f - 0.05f;
		add[1] = g/1536.f - 0.05f;
		add[2] = b/1536.f - 0.05f;
#ifdef RW_D3D9
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, mult, 1);
		rw::d3d::d3ddevice->SetPixelShaderConstantF(11, add, 1);

		rw::d3d::im2dOverridePS = contrast_PS;
#endif
#ifdef RW_OPENGL
		rw::gl3::im2dOverrideShader = contrast;
		contrast->use();
		glUniform3fv(contrast->uniformLocations[u_contrastMult], 1, mult);
		glUniform3fv(contrast->uniformLocations[u_contrastAdd], 1, add);
#endif
	}else{
		float f = Intensity;
		float blurcolors[4];
		blurcolors[0] = r*f/255.0f;
		blurcolors[1] = g*f/255.0f;
		blurcolors[2] = b*f/255.0f;
		blurcolors[3] = 30/255.0f;
#ifdef RW_D3D9
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, blurcolors, 1);
#ifdef POSTFX_TONEMAP
		float tonemapParams[4] = {
			TonemapACES ? 1.0f : 0.0f,
			TonemapGamma ? 1.0f : 0.0f,
			Exposure,
			Saturation
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(11, tonemapParams, 1);

		// Aspect (.w) is camera width/height so the vignette stays circular.
		uint32 camW = RwRasterGetWidth(RwCameraGetRaster(cam));
		uint32 camH = RwRasterGetHeight(RwCameraGetRaster(cam));
		float aspect = camH > 0 ? (float)camW / (float)camH : 1.0f;
		float vignetteParams[4] = {
			VignetteIntensity,
			VignetteSoftness,
			VignetteRoundness,
			aspect
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(12, vignetteParams, 1);

		float caParams[4] = { CAStrength, CADistanceScale, 0.0f, 0.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(13, caParams, 1);
#else
		float tonemapParams[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(11, tonemapParams, 1);
		float vignetteParams[4] = { 0.0f, 0.5f, 1.0f, 1.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(12, vignetteParams, 1);
		float caParams[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(13, caParams, 1);
#endif
		rw::d3d::im2dOverridePS = colourfilterVC_PS;
#endif
#ifdef RW_OPENGL
		rw::gl3::im2dOverrideShader = colourFilterVC;
		colourFilterVC->use();
		glUniform4fv(colourFilterVC->uniformLocations[u_blurcolor], 1, blurcolors);
#endif
	}
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
#ifdef RW_D3D9
	rw::d3d::im2dOverridePS = nil;
#endif
#ifdef RW_OPENGL
	rw::gl3::im2dOverrideShader = nil;
#endif
}

#ifdef POSTFX_HDR

// Bind a CAMERATEXTURE raster as a PS sampler slot (s1..s15). Slot 0 is
// owned by the rwRENDERSTATETEXTURERASTER render-state path.
static void
BindRasterToSampler(int slot, RwRaster *raster)
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

static inline float lerp_f(float a, float b, float t){ return a + (b - a) * t; }

void
CPostFX::UpdateIBL(void)
{
	// Pull the current sky-top / sky-bottom colours from the time-cycle —
	// they already encode the dawn-noon-dusk-night progression. Top drives
	// the zenith colour, bottom drives the horizon, and the ground is a
	// muted earthy tint biased toward the horizon (so a green afternoon
	// horizon doesn't make pavements glow green).
	float top[3] = {
		(float)CTimeCycle::GetSkyTopRed()   * IblExposure,
		(float)CTimeCycle::GetSkyTopGreen() * IblExposure,
		(float)CTimeCycle::GetSkyTopBlue()  * IblExposure,
	};
	float bot[3] = {
		(float)CTimeCycle::GetSkyBottomRed()   * IblExposure,
		(float)CTimeCycle::GetSkyBottomGreen() * IblExposure,
		(float)CTimeCycle::GetSkyBottomBlue()  * IblExposure,
	};
	// Ground colour: lerp from neutral mid-grey toward a desaturated horizon
	// tint based on IblGroundTint. Stays plausible at night (everything goes
	// blueish dark) and at dusk (warms slightly).
	float grey[3] = { 0.06f, 0.055f, 0.05f };
	float ground[3] = {
		grey[0] + (bot[0] * 0.25f - grey[0]) * IblGroundTint,
		grey[1] + (bot[1] * 0.25f - grey[1]) * IblGroundTint,
		grey[2] + (bot[2] * 0.25f - grey[2]) * IblGroundTint,
	};

	// Lightning illumination — when CWeather::LightningFlash is on, blast
	// the IBL toward bright cool-white so the whole world briefly lights
	// up. We keep the original gradient direction so shadows still read
	// correctly; only the magnitude is boosted. Visible even with IBL
	// otherwise disabled because we force-enable it for the flash.
	float lightningBoost = 0.0f;
	bool lightningOverride = false;
	if(CWeather::LightningFlash){
		lightningBoost = 1.0f;
		lightningOverride = true;
	}
	if(lightningOverride){
		// Cool blue-white at ~4× normal sky-top brightness, applied
		// uniformly across the gradient so every face of the geometry
		// catches it (lightning lights everything, not just up-facing).
		const float flashCol = 1.6f;
		float flashRGB[3] = { flashCol * 0.95f, flashCol, flashCol * 1.1f };
		top[0]    = lerp_f(top[0],    flashRGB[0], lightningBoost);
		top[1]    = lerp_f(top[1],    flashRGB[1], lightningBoost);
		top[2]    = lerp_f(top[2],    flashRGB[2], lightningBoost);
		bot[0]    = lerp_f(bot[0],    flashRGB[0] * 0.8f, lightningBoost);
		bot[1]    = lerp_f(bot[1],    flashRGB[1] * 0.8f, lightningBoost);
		bot[2]    = lerp_f(bot[2],    flashRGB[2] * 0.8f, lightningBoost);
		ground[0] = lerp_f(ground[0], flashRGB[0] * 0.4f, lightningBoost);
		ground[1] = lerp_f(ground[1], flashRGB[1] * 0.4f, lightningBoost);
		ground[2] = lerp_f(ground[2], flashRGB[2] * 0.4f, lightningBoost);
	}
	float liveIntensity = IblIntensity;
	bool liveEnabled = IblEnabled;
	if(lightningOverride){
		// Crank the intensity so the flash punches through any tonemap.
		liveIntensity = lerp_f(IblIntensity, 2.5f, lightningBoost);
		liveEnabled = true;
	}

	// Wet-surface signal — pulls from CWeather::WetRoads (which itself
	// blends WetRoads target during rain → dries on time-cycle). Clamp
	// the engine's signal × user intensity multiplier so a dry day with
	// the user's slider at max doesn't still look soaked.
	float wetness = 0.0f;
	if(WetSurfacesEnable){
		// CWeather::WetRoads ∈ [0,1]; CWeather::Rain ∈ [0,1].
		// Use the larger so heavy rain instantly puddles even before
		// WetRoads catches up.
		wetness = (CWeather::WetRoads > CWeather::Rain ? CWeather::WetRoads : CWeather::Rain);
		wetness = wetness * WetSurfacesIntensity;
		if(wetness > 1.0f) wetness = 1.0f;
	}

#ifdef RW_D3D9
	rw::d3d::iblEnabled = liveEnabled;
	rw::d3d::setIblColors(top, bot, ground, liveIntensity, IblHorizonExp);
	rw::d3d::setWetness(wetness, WetSurfacesDiffuse, WetSurfacesSpec, WetSurfacesPower);
	rw::d3d::uploadIBL();
#endif
}

void
CPostFX::RenderSSAO(RwCamera *cam)
{
	if(!CGBuffer::HdrEnabled || !CGBuffer::GbufEnabled || !SsaoEnable)
		return;
	if(CGBuffer::pGbufNormalDepth == nil || ssao_PS == nil || ssaoBlur_PS == nil ||
	   pSsaoA == nil || pSsaoB == nil || ssaoCamA == nil || ssaoCamB == nil)
		return;

	PUSH_RENDERGROUP("CPostFX::RenderSSAO");

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

	float farClip = RwCameraGetFarClipPlane(cam);
	float cw = (float)RwRasterGetWidth(RwCameraGetRaster(cam));
	float ch = (float)RwRasterGetHeight(RwCameraGetRaster(cam));

	// Pass 1: SSAO sampling — G-buffer -> pSsaoA
	RwCameraEndUpdate(cam);
	RwCameraBeginUpdate((RwCamera*)ssaoCamA);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CGBuffer::pGbufNormalDepth);
	{
#ifdef RW_D3D9
		float p[4] = { SsaoRadius, SsaoBias, SsaoIntensity, farClip };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, p, 1);
		float t[4] = { 1.0f/cw, 1.0f/ch, 0.0f, 0.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(11, t, 1);
		// Contact AO params at c12. The shader branches on .x so when this
		// is 0 (player toggled it off) the inner [branch] is statically
		// skipped — no perf cost over the legacy hemisphere-only path.
		float cAO[4] = {
			SsaoContactStrength,
			SsaoContactRadius,
			SsaoContactMaxDz,
			1.0f,	// inner bias multiplier; 1.0 reuses SsaoBias directly
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(12, cAO, 1);

		// Contact shadows — project the sun direction into view+screen
		// space so the shader knows which way to march. We can take the
		// view-space sun direction (transform world dir by view matrix's
		// rotation), then build a per-step UV delta proportional to the
		// X/Y screen components.
		{
			rw::Camera *rwcam = (rw::Camera*)cam;
			rw::V3d sunW = { 0, 0, 1 };
			if(pDirect){
				rw::V3d a = pDirect->getFrame()->getLTM()->at;
				// pDirect points away from the sun — flip
				sunW.x = -a.x; sunW.y = -a.y; sunW.z = -a.z;
			}
			// Transform to view space using the view matrix's rotation
			// (translation doesn't affect a direction vector).
			rw::RawMatrix *vm = &rwcam->devView;
			float sx = sunW.x * vm->right.x + sunW.y * vm->up.x + sunW.z * vm->at.x;
			float sy = sunW.x * vm->right.y + sunW.y * vm->up.y + sunW.z * vm->at.y;
			float sz = sunW.x * vm->right.z + sunW.y * vm->up.z + sunW.z * vm->at.z;
			float slen = sqrtf(sx*sx + sy*sy + sz*sz);
			if(slen > 1e-5f){ sx/=slen; sy/=slen; sz/=slen; }
			// Per-step UV delta — roughly (pixels-per-step / resolution)
			// scaled by the view-space X/Y components.
			const float stepLenPx = 3.5f;	// ~3-4 texels per step
			float uvDx =  sx * stepLenPx * (1.0f/cw);
			float uvDy = -sy * stepLenPx * (1.0f/ch);	// D3D9 Y-flip
			// World-space Z delta per step is roughly stepLenPx * pixelSizeAtFarClip * sunZ.
			// Simpler: depth delta along the view ray ≈ sz * stepLenPx * (avgViewZ / cw).
			// We pass a unit "delta per step" multiplier; the shader scales
			// it by ssaoContactShadowTuning.y (thickness in metres).
			float depthDelta = -sz;	// negative sz = sun is behind camera; flip sign
			float csDir[4] = { uvDx, uvDy, depthDelta, ContactShadowStrength };
			rw::d3d::d3ddevice->SetPixelShaderConstantF(13, csDir, 1);
			float csTune[4] = {
				(float)ContactShadowSteps,
				ContactShadowThickness,
				ContactShadowBias,
				0.0f,
			};
			rw::d3d::d3ddevice->SetPixelShaderConstantF(14, csTune, 1);
		}
		// Pick the AO algorithm. GTAO has a different sampling model
		// (horizon angles vs hemisphere) but shares the same c10/c11
		// constants for radius/bias/intensity/farClip + texel + noise.
		rw::d3d::im2dOverridePS = (SsaoAlgorithm == 1 && gtao_PS) ? gtao_PS : ssao_PS;
#endif
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, SsaoVertex, 4, Index, 6);
	}
	RwCameraEndUpdate((RwCamera*)ssaoCamA);

	// Pass 2: bilateral blur H — pSsaoA -> pSsaoB. Also bind G-buffer on s1
	// for the depth weights.
	RwCameraBeginUpdate((RwCamera*)ssaoCamB);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pSsaoA);
	BindRasterToSampler(1, CGBuffer::pGbufNormalDepth);
	{
#ifdef RW_D3D9
		float p[4] = { 1.0f/(float)g_ssaoW, 0.0f, 6.0f, farClip };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, p, 1);
		rw::d3d::im2dOverridePS = ssaoBlur_PS;
#endif
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, SsaoVertex, 4, Index, 6);
	}
	RwCameraEndUpdate((RwCamera*)ssaoCamB);

	// Pass 3: bilateral blur V — pSsaoB -> pSsaoA
	RwCameraBeginUpdate((RwCamera*)ssaoCamA);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pSsaoB);
	BindRasterToSampler(1, CGBuffer::pGbufNormalDepth);
	{
#ifdef RW_D3D9
		float p[4] = { 0.0f, 1.0f/(float)g_ssaoH, 6.0f, farClip };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, p, 1);
		rw::d3d::im2dOverridePS = ssaoBlur_PS;
#endif
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, SsaoVertex, 4, Index, 6);
	}
	RwCameraEndUpdate((RwCamera*)ssaoCamA);

	// Unbind s1 to keep other passes from accidentally reading the G-buffer.
	BindRasterToSampler(1, nil);
	RwCameraBeginUpdate(cam);

#ifdef RW_D3D9
	rw::d3d::im2dOverridePS = nil;
#endif

	POP_RENDERGROUP();
}

void
CPostFX::RenderSSR(RwCamera *cam)
{
	if(!CGBuffer::HdrEnabled || !CGBuffer::GbufEnabled || !SsrEnable)
		return;
	if(CGBuffer::pGbufNormalDepth == nil || CGBuffer::pHdrScene == nil ||
	   ssr_PS == nil || pSsrA == nil || ssrCam == nil)
		return;

	PUSH_RENDERGROUP("CPostFX::RenderSSR");

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

	RwCameraEndUpdate(cam);
	RwCameraBeginUpdate((RwCamera*)ssrCam);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CGBuffer::pGbufNormalDepth);
	BindRasterToSampler(1, CGBuffer::pHdrScene);

#ifdef RW_D3D9
	{
		rw::Camera *rwcam = (rw::Camera*)cam;
		rw::V3d camPos = rwcam->getFrame()->getLTM()->pos;
		float farClip = rwcam->farPlane;

		// c10: camera + farClip
		float cCam[4] = { camPos.x, camPos.y, camPos.z, farClip };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, cCam, 1);

		// c11..c14: frustum corner rays — same layout/order as the
		// volumetric fog (TL, TR, BR, BL).
		const rw::V3d *fc = rwcam->frustumCorners;
		float corners[4][4] = {
			{ fc[0].x, fc[0].y, fc[0].z, 0 },
			{ fc[1].x, fc[1].y, fc[1].z, 0 },
			{ fc[2].x, fc[2].y, fc[2].z, 0 },
			{ fc[3].x, fc[3].y, fc[3].z, 0 },
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(11, &corners[0][0], 4);

		// c15..c18: world-to-clip matrix. librw already maintains devView
		// (world→view) and devProj (view→clip); combine them in row-major
		// form for SetPixelShaderConstantF which uploads as float4×N.
		rw::RawMatrix vp;
		rw::RawMatrix::mult(&vp, &rwcam->devView, &rwcam->devProj);
		rw::d3d::d3ddevice->SetPixelShaderConstantF(15, (const float*)&vp, 4);

		// c19: params
		float pp[4] = {
			SsrMaxDistance,
			(float)SsrStepCount,
			SsrThickness,
			SsrStrength,
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(19, pp, 1);

		rw::d3d::im2dOverridePS = ssr_PS;
	}
#endif

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, SsaoVertex, 4, Index, 6);

	RwCameraEndUpdate((RwCamera*)ssrCam);
	BindRasterToSampler(1, nil);
	RwCameraBeginUpdate(cam);

#ifdef RW_D3D9
	rw::d3d::im2dOverridePS = nil;
#endif

	POP_RENDERGROUP();
}

void
CPostFX::RenderDoF(RwCamera *cam)
{
	if(!CGBuffer::HdrEnabled || !CGBuffer::GbufEnabled || !DofEnable)
		return;
	if(CGBuffer::pHdrScene == nil || CGBuffer::pGbufNormalDepth == nil ||
	   dof_PS == nil || pDofScratch == nil || dofCam == nil)
		return;

	PUSH_RENDERGROUP("CPostFX::RenderDoF");

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

	// pHdrScene → pDofScratch (blurred), then copy back via raster blit.
	float cw = (float)RwRasterGetWidth(RwCameraGetRaster(cam));
	float ch = (float)RwRasterGetHeight(RwCameraGetRaster(cam));

	RwCameraEndUpdate(cam);
	RwCameraBeginUpdate((RwCamera*)dofCam);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CGBuffer::pHdrScene);
	BindRasterToSampler(1, CGBuffer::pGbufNormalDepth);

#ifdef RW_D3D9
	{
		float farClip = ((rw::Camera*)cam)->farPlane;
		float pp[4] = { farClip, DofFocusDistance, DofFocusRange, DofAperture };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, pp, 1);
		float texel[4] = { 1.0f/cw, 1.0f/ch, 0.0f, 0.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(11, texel, 1);
		rw::d3d::im2dOverridePS = dof_PS;
	}
#endif

	// Use the camera-sized vertex quad (same one ResolveHDR uses).
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, HdrResolveVertex, 4, Index, 6);

	RwCameraEndUpdate((RwCamera*)dofCam);
	BindRasterToSampler(1, nil);

#ifdef RW_D3D9
	rw::d3d::im2dOverridePS = nil;
	// Swap pHdrScene and pDofScratch so the downstream ResolveHDR reads
	// the blurred image. Cheap pointer swap, no extra copy needed since
	// both rasters are the same format/size.
	RwRaster *tmp = CGBuffer::pHdrScene;
	CGBuffer::pHdrScene = pDofScratch;
	pDofScratch = tmp;
#endif

	RwCameraBeginUpdate(cam);

	POP_RENDERGROUP();
}

void
CPostFX::ResolveHDR(RwCamera *cam)
{
	if(!CGBuffer::HdrEnabled || CGBuffer::pHdrScene == nil || hdrResolve_PS == nil)
		return;

	PUSH_RENDERGROUP("CPostFX::ResolveHDR");

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CGBuffer::pHdrScene);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

	// Bind the AO buffer on sampler 1 — hdrResolve_PS reads it when the
	// SSAO strength is non-zero. Unbind on exit.
	bool ssaoActive = SsaoEnable && pSsaoA != nil && CGBuffer::GbufEnabled;
	if(ssaoActive)
		BindRasterToSampler(1, pSsaoA);

	// Bind the G-buffer (slot 1 = packed world-normal + linear depth) on
	// sampler 2. Required by the volumetric fog ray-march to find the
	// march endpoint per pixel; sky/uncovered pixels read alpha=0 and the
	// shader treats them as "march to volParams.z". Also reused by the
	// SSR compose for the per-pixel world normal.
	bool ssrActive = SsrEnable && CGBuffer::GbufEnabled && pSsrA != nil;
	bool volFogActive = VolFogEnable && CGBuffer::GbufEnabled
	                 && CGBuffer::pGbufNormalDepth != nil;
	bool gbufNeeded = volFogActive || ssrActive;
	if(gbufNeeded)
		BindRasterToSampler(2, CGBuffer::pGbufNormalDepth);
	if(ssrActive)
		BindRasterToSampler(3, pSsrA);

#ifdef RW_D3D9
	// .x = exposure, .y = ACES toggle, .z = gamma toggle, .w = saturation
	float params[4] = {
# ifdef POSTFX_TONEMAP
		Exposure,
		TonemapACES ? 1.0f : 0.0f,
		TonemapGamma ? 1.0f : 0.0f,
		Saturation,
# else
		1.0f, 0.0f, 0.0f, 1.0f,
# endif
	};
	rw::d3d::d3ddevice->SetPixelShaderConstantF(10, params, 1);

	float ssaoMix[4] = {
		ssaoActive ? SsaoStrength : 0.0f,
		SsaoPower,
		0.0f, 0.0f
	};
	rw::d3d::d3ddevice->SetPixelShaderConstantF(11, ssaoMix, 1);

	// --- Volumetric fog constants (c12..c19) -----------------------------
	// c12: camera world pos + farClip; the ray-march walks viewZ along the
	//      view ray and unpacks gbuf.a*farClip = viewZ to size the loop.
	{
		rw::Camera *rwcam = (rw::Camera*)cam;
		rw::V3d camPos = rwcam->getFrame()->getLTM()->pos;
		float farClip = rwcam->farPlane;
		float volCam[4] = { camPos.x, camPos.y, camPos.z, farClip };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(12, volCam, 1);

		// c13..c16: world-space rays through the 4 screen corners. librw
		// already computes frustumCorners[0..3] as unit-forward vectors
		// from camera position (length along camera.at = 1, lateral comp
		// scaled by viewWindow). Order: 0=TL, 1=TR, 2=BR, 3=BL. We just
		// repackage them as float4s for c-register upload.
		const rw::V3d *fc = rwcam->frustumCorners;
		float volRays[4][4] = {
			{ fc[0].x, fc[0].y, fc[0].z, 0.0f },	// TL → c13
			{ fc[1].x, fc[1].y, fc[1].z, 0.0f },	// TR → c14
			{ fc[2].x, fc[2].y, fc[2].z, 0.0f },	// BR → c15
			{ fc[3].x, fc[3].y, fc[3].z, 0.0f },	// BL → c16
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(13, &volRays[0][0], 4);

		// c17: direction TOWARD the sun + HG g. pDirect->at points away
		// from the sun (sun light direction = sun → ground), so negate.
		float sunDir[4] = { 0, 0, 1, VolFogHG };
		if(volFogActive && pDirect != nullptr){
			rw::V3d a = pDirect->getFrame()->getLTM()->at;
			float len = sqrtf(a.x*a.x + a.y*a.y + a.z*a.z);
			if(len > 1e-5f){
				sunDir[0] = -a.x / len;
				sunDir[1] = -a.y / len;
				sunDir[2] = -a.z / len;
			}
		}
		rw::d3d::d3ddevice->SetPixelShaderConstantF(17, sunDir, 1);

		// c18: scattering colour (sun×boost, HDR-aware) + base density.
		// Lightning replaces the warm sun colour with a cool bright flash
		// so the fog itself glows during the strike — adds a huge sense
		// of atmosphere to night storms. Underwater swaps to a tinted
		// blue-green absorption + much higher density so visibility
		// drops quickly with distance, matching how light dies in water.
		float r = DirectionalLightColourForFrame.red   * VolFogSunBoost;
		float g = DirectionalLightColourForFrame.green * VolFogSunBoost;
		float b = DirectionalLightColourForFrame.blue  * VolFogSunBoost;
		float densityNow = VolFogDensity;
		float groundZNow = VolFogGroundZ;
		float maxDistNow = VolFogMaxDist;
		bool underwater = false;
		{
			rw::V3d cp = camPos;
			float waterY = -10000.0f;
			if(CWaterLevel::GetWaterLevelNoWaves(cp.x, cp.y, cp.z, &waterY)){
				underwater = (cp.z < waterY);
			}
		}
		if(underwater){
			// Greenish-blue absorption tint with no sun in-scatter (the
			// sun is filtered out by the water column). Density is high
			// so distant objects vanish quickly. groundZ is moved well
			// above the camera so the height falloff stops thinning
			// fog with depth.
			r = 0.08f; g = 0.18f; b = 0.28f;
			densityNow = 0.12f;
			groundZNow = camPos.z + 200.0f;
			maxDistNow = 60.0f;
		}else if(CWeather::LightningFlash){
			r = 2.0f; g = 2.1f; b = 2.4f;	// cool-white, HDR
			densityNow = VolFogDensity * 1.8f;	// thicker fog so the flash carries
		}
		float volCol[4] = { r, g, b, densityNow };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(18, volCol, 1);

		// c19: height falloff / ground / max-march / strength-lerp gate.
		// Underwater forces strength to 1.0 so the swap is visible even
		// when the player has volumetric fog disabled in the menu — the
		// alternative is wrong-looking clear underwater visibility.
		float volPar[4] = {
			underwater ? 0.005f : VolFogHeightFalloff,
			groundZNow,
			maxDistNow,
			underwater ? 1.0f : (volFogActive ? VolFogStrength : 0.0f),
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(19, volPar, 1);

		// c20: SSR compose strength + Fresnel F0 bias.
		float ssrPar[4] = {
			ssrActive ? SsrStrength : 0.0f,
			SsrFresnelBias,
			0.0f, 0.0f,
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(20, ssrPar, 1);

		// c21: camera world pos again (the SSR compose path needs it but
		// volCamera is already loaded; redundant write keeps the bindings
		// independent so we can drop volumetric without breaking SSR).
		float cView[4] = { camPos.x, camPos.y, camPos.z, 0.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(21, cView, 1);
	}

	rw::d3d::im2dOverridePS = hdrResolve_PS;
#endif

	// Use the camera-sized vertex quad (not Vertex[], which is pow2-sized
	// for pBackBuffer). UV=0..1 over the quad maps exactly to the
	// camera-sized pHdrScene without any stretching.
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, HdrResolveVertex, 4, Index, 6);

#ifdef RW_D3D9
	rw::d3d::im2dOverridePS = nil;
	if(ssaoActive)
		BindRasterToSampler(1, nil);
	if(gbufNeeded)
		BindRasterToSampler(2, nil);
	if(ssrActive)
		BindRasterToSampler(3, nil);
#endif
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	POP_RENDERGROUP();
}
#endif

void
CPostFX::RenderMotionBlur(RwCamera *cam, uint32 blur)
{
	if(blur == 0)
		return;

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	RwIm2DVertexSetIntRGBA(&Vertex[0], 255, 255, 255, blur);
	RwIm2DVertexSetIntRGBA(&Vertex[1], 255, 255, 255, blur);
	RwIm2DVertexSetIntRGBA(&Vertex[2], 255, 255, 255, blur);
	RwIm2DVertexSetIntRGBA(&Vertex[3], 255, 255, 255, blur);

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
}

#ifdef POSTFX_BLOOM
void
CPostFX::RenderBloom(RwCamera *cam)
{
#ifdef RW_D3D9
	if(!BloomEnable || brightpass_PS == nil || bloomBlur_PS == nil ||
	   bloomComposite_PS == nil || bloomCamA == nil || bloomCamB == nil ||
	   pBloomA == nil || pBloomB == nil)
		return;

	PUSH_RENDERGROUP("CPostFX::RenderBloom");

	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

	// 1. Bright-pass: pBackBuffer -> pBloomA
	RwCameraEndUpdate(cam);
	RwCameraBeginUpdate((RwCamera*)bloomCamA);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBackBuffer);
	{
		float params[4] = { BloomThreshold, BloomKnee, 1.0f, 0.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, params, 1);
		// brightpass also needs the source texel size for its 5-tap prefilter.
		float texel[4] = {
			1.0f / (float)g_postfxRtWidth,
			1.0f / (float)g_postfxRtHeight,
			0.0f, 0.0f
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(11, texel, 1);
		rw::d3d::im2dOverridePS = brightpass_PS;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
	}
	RwCameraEndUpdate((RwCamera*)bloomCamA);

	// 2. Blur horizontal: pBloomA -> pBloomB
	RwCameraBeginUpdate((RwCamera*)bloomCamB);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBloomA);
	{
		float dir[4] = { 1.0f / (float)g_postfxRtWidth, 0.0f, 0.0f, 0.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, dir, 1);
		rw::d3d::im2dOverridePS = bloomBlur_PS;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
	}
	RwCameraEndUpdate((RwCamera*)bloomCamB);

	// 3. Blur vertical: pBloomB -> pBloomA
	RwCameraBeginUpdate((RwCamera*)bloomCamA);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBloomB);
	{
		float dir[4] = { 0.0f, 1.0f / (float)g_postfxRtHeight, 0.0f, 0.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, dir, 1);
		rw::d3d::im2dOverridePS = bloomBlur_PS;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
	}
	RwCameraEndUpdate((RwCamera*)bloomCamA);

	// 4. Additive composite onto the main camera target.
	RwCameraBeginUpdate(cam);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBloomA);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	{
		float mix[4] = { BloomIntensity, BloomSaturation, 0.0f, 0.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, mix, 1);
		rw::d3d::im2dOverridePS = bloomComposite_PS;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
	}
	rw::d3d::im2dOverridePS = nil;
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	POP_RENDERGROUP();
#endif
}
#endif

#ifdef POSTFX_GODRAYS
void
CPostFX::RenderGodRays(RwCamera *cam)
{
#ifdef RW_D3D9
	if(!GodRaysEnable || godrays_PS == nil || pBloomA == nil)
		return;

	// Sun direction (world space) — only render when it's roughly in front
	// of the camera so the rays converge to a visible point.
	CVector sunDir = CTimeCycle::GetSunDirection();
	rw::Camera *rwcam = (rw::Camera*)cam;
	rw::V3d viewDir = rwcam->getFrame()->getLTM()->at;
	float facing = sunDir.x*viewDir.x + sunDir.y*viewDir.y + sunDir.z*viewDir.z;
	if(facing < 0.05f)
		return;

	// Project a far point along sunDir into clip space.
	rw::V3d camPos = rwcam->getFrame()->getLTM()->pos;
	rw::V3d sunWorld;
	sunWorld.x = camPos.x + sunDir.x * 1000.0f;
	sunWorld.y = camPos.y + sunDir.y * 1000.0f;
	sunWorld.z = camPos.z + sunDir.z * 1000.0f;

	rw::RawMatrix viewProj;
	rw::RawMatrix::mult(&viewProj, &rwcam->devView, &rwcam->devProj);

	// Column-major 4x4 multiply: clipCol = M * [x y z 1]^T
	float clipX = sunWorld.x*viewProj.right.x  + sunWorld.y*viewProj.up.x  + sunWorld.z*viewProj.at.x  + viewProj.pos.x;
	float clipY = sunWorld.x*viewProj.right.y  + sunWorld.y*viewProj.up.y  + sunWorld.z*viewProj.at.y  + viewProj.pos.y;
	float clipW = sunWorld.x*viewProj.rightw   + sunWorld.y*viewProj.upw   + sunWorld.z*viewProj.atw   + viewProj.posw;
	if(clipW <= 0.0f)
		return;

	float sunU = (clipX/clipW) * 0.5f + 0.5f;
	float sunV = -(clipY/clipW) * 0.5f + 0.5f;
	// Skip when the sun is well off the screen — rays would fly into the
	// edge and look broken.
	if(sunU < -0.5f || sunU > 1.5f || sunV < -0.5f || sunV > 1.5f)
		return;

	PUSH_RENDERGROUP("CPostFX::RenderGodRays");

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBloomA);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

	float godParams[4] = { sunU, sunV, GodRaysDensity, GodRaysDecay };
	rw::d3d::d3ddevice->SetPixelShaderConstantF(10, godParams, 1);

	// Warm tint that fades as the sun gets close to the horizon — uses the
	// dot-with-view facing factor as a cheap occlusion proxy.
	float facingClamped = (facing > 1.0f) ? 1.0f : facing;
	float godColor[4] = { 1.0f, 0.92f, 0.78f, GodRaysExposure * facingClamped };
	rw::d3d::d3ddevice->SetPixelShaderConstantF(11, godColor, 1);

	rw::d3d::im2dOverridePS = godrays_PS;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
	rw::d3d::im2dOverridePS = nil;

	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	POP_RENDERGROUP();
#endif
}
#endif

#ifdef POSTFX_HDR
void
CPostFX::RenderTAA(RwCamera *cam)
{
#ifdef RW_D3D9
	if(!TaaEnable || taa_PS == nil || pBackBuffer == nil ||
	   pTaaHistA == nil || pTaaHistB == nil ||
	   taaCamA == nil || taaCamB == nil)
		return;

	PUSH_RENDERGROUP("CPostFX::RenderTAA");

	// 1. Capture the current backbuffer into pBackBuffer so the TAA pass
	//    has a stable read of the just-tonemapped colour. GetBackBuffer
	//    does a fast surface copy (StretchRect).
	GetBackBuffer(cam);

	// 2. Pick the current history slot (read from) and the next slot
	//    (write to). Ping-pong each frame.
	RwRaster *histRead  = (TaaFrameIdx == 0) ? pTaaHistA : pTaaHistB;
	RwRaster *histWrite = (TaaFrameIdx == 0) ? pTaaHistB : pTaaHistA;
	rw::Camera *writeCam = (rw::Camera*)((TaaFrameIdx == 0) ? taaCamB : taaCamA);

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

	// 3. Blend pass: pBackBuffer (s0=current) + histRead (s1=previous) +
	//    gbuf (s2) -> histWrite. The gbuf is required for the reprojection
	//    step in taa_PS — without it the history is sampled at the same UV
	//    which only works for static cameras.
	RwCameraEndUpdate(cam);
	RwCameraBeginUpdate((RwCamera*)writeCam);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBackBuffer);
	BindRasterToSampler(1, histRead);
	bool gbufBound = false;
	if(CGBuffer::GbufEnabled && CGBuffer::pGbufNormalDepth){
		BindRasterToSampler(2, CGBuffer::pGbufNormalDepth);
		gbufBound = true;
	}
	{
		float p[4] = { TaaBlend, TaaClamp, 0.0f, 0.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, p, 1);
		float t[4] = { 1.0f / (float)g_postfxRtWidth, 1.0f / (float)g_postfxRtHeight, 0.0f, 0.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(11, t, 1);

		// Reprojection constants — same layout as the volumetric fog /
		// SSR (camera + 4 corner rays + prev viewProj).
		rw::Camera *rwcam = (rw::Camera*)cam;
		rw::V3d camPos = rwcam->getFrame()->getLTM()->pos;
		float cCam[4] = { camPos.x, camPos.y, camPos.z, rwcam->farPlane };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(12, cCam, 1);
		const rw::V3d *fc = rwcam->frustumCorners;
		float corners[4][4] = {
			{ fc[0].x, fc[0].y, fc[0].z, 0 },	// TL
			{ fc[1].x, fc[1].y, fc[1].z, 0 },	// TR
			{ fc[2].x, fc[2].y, fc[2].z, 0 },	// BR
			{ fc[3].x, fc[3].y, fc[3].z, 0 },	// BL
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(13, &corners[0][0], 4);
		// Previous frame's view-projection. Captured at the end of the
		// previous RenderTAA call; on the first frame it's still identity,
		// which makes the reproject UV match the current one — same as
		// the old camera-only behaviour, no surprise.
		rw::d3d::d3ddevice->SetPixelShaderConstantF(17, (const float*)&taaPrevViewProj, 4);

		rw::d3d::im2dOverridePS = taa_PS;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
		rw::d3d::im2dOverridePS = nil;
	}
	BindRasterToSampler(1, nil);
	if(gbufBound)
		BindRasterToSampler(2, nil);
	RwCameraEndUpdate((RwCamera*)writeCam);

	// Cache this frame's view×projection for next frame's reprojection.
	{
		rw::Camera *rwcam = (rw::Camera*)cam;
		rw::RawMatrix::mult(&taaPrevViewProj, &rwcam->devView, &rwcam->devProj);
	}

	// 4. Restore the main camera and copy histWrite -> backbuffer so the
	//    user sees the resolved TAA result.
	RwCameraBeginUpdate(cam);
	RwRasterPushContext(RwCameraGetRaster(cam));
	RwRasterRenderFast(histWrite, 0, 0);
	RwRasterPopContext();

	TaaFrameIdx ^= 1;

	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	POP_RENDERGROUP();
#endif
}
#endif

#ifdef POSTFX_FXAA
void
CPostFX::RenderFXAA(RwCamera *cam)
{
#ifdef RW_D3D9
	if(!FxaaEnable || fxaa_PS == nil || pBackBuffer == nil)
		return;

	PUSH_RENDERGROUP("CPostFX::RenderFXAA");

	// Capture the current camera output as input for FXAA.
	GetBackBuffer(cam);

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBackBuffer);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

	float params[4] = {
		1.0f / (float)g_postfxRtWidth,
		1.0f / (float)g_postfxRtHeight,
		FxaaStrength,
		0.0f
	};
	rw::d3d::d3ddevice->SetPixelShaderConstantF(10, params, 1);
	rw::d3d::im2dOverridePS = fxaa_PS;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
	rw::d3d::im2dOverridePS = nil;

	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	POP_RENDERGROUP();
#endif
}
#endif

bool
CPostFX::NeedBackBuffer(void)
{
	// Current frame -- needed for non-blur effect
	switch(EffectSwitch){
	case POSTFX_OFF:
	case POSTFX_SIMPLE:
		// no actual rendering here
		return false;
	case POSTFX_NORMAL:
		if(MotionBlurOn)
			return false;
		else
			return true;
	case POSTFX_MOBILE:
		return true;
	}
	return false;
}

bool
CPostFX::NeedFrontBuffer(int32 type)
{
	// Last frame -- needed for motion blur
	if(CMBlur::Drunkness > 0.0f)
		return true;
	if(type == MOTION_BLUR_SNIPER)
		return true;

	switch(EffectSwitch){
	case POSTFX_OFF:
	case POSTFX_SIMPLE:
		// no actual rendering here
		return false;
	case POSTFX_NORMAL:
		if(MotionBlurOn)
			return true;
		else
			return false;
	case POSTFX_MOBILE:
		return false;
	}
	return false;
}

void
CPostFX::GetBackBuffer(RwCamera *cam)
{
	RwRasterPushContext(pBackBuffer);
	RwRasterRenderFast(RwCameraGetRaster(cam), 0, 0);
	RwRasterPopContext();
}

void
CPostFX::Render(RwCamera *cam, uint32 red, uint32 green, uint32 blue, uint32 blur, int32 type, uint32 bluralpha)
{
	PUSH_RENDERGROUP("CPostFX::Render");

	if(pFrontBuffer == nil)
		Open(cam);
	assert(pFrontBuffer);
	assert(pBackBuffer);

	if(type == MOTION_BLUR_LIGHT_SCENE){
		SmoothColor(red, green, blue, blur);
		red = AvgRed;
		green = AvgGreen;
		blue = AvgBlue;
		blur = AvgAlpha;
	}

	if(NeedBackBuffer())
		GetBackBuffer(cam);

	DefinedState();

	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);

	if(type == MOTION_BLUR_SNIPER){
		if(!bJustInitialised)
			RenderOverlaySniper(cam, red, green, blue, blur);
	}else switch(EffectSwitch){
	case POSTFX_OFF:
	case POSTFX_SIMPLE:
		// no actual rendering here
		break;
	case POSTFX_NORMAL:
		if(MotionBlurOn){
			if(!bJustInitialised)
				RenderOverlayBlur(cam, red, green, blue, blur);
		}else{
			RenderOverlayShader(cam, red, green, blue, blur);
		}
		break;
	case POSTFX_MOBILE:
		RenderOverlayShader(cam, red, green, blue, blur);
		break;
	}

#ifdef POSTFX_BLOOM
	if(BloomEnable && !bJustInitialised && type != MOTION_BLUR_SNIPER &&
	   EffectSwitch != POSTFX_OFF && EffectSwitch != POSTFX_SIMPLE)
		RenderBloom(cam);
#endif

#ifdef POSTFX_GODRAYS
	// God rays reuse the (already populated) bright-pass buffer from bloom,
	// so they must run after RenderBloom but before motion blur / FXAA.
	if(GodRaysEnable && !bJustInitialised && type != MOTION_BLUR_SNIPER &&
	   EffectSwitch != POSTFX_OFF && EffectSwitch != POSTFX_SIMPLE)
		RenderGodRays(cam);
#endif

#ifdef POSTFX_HDR
	// TAA and FXAA are mutually exclusive AA strategies — TAA runs first
	// and disables FXAA for this frame when it's active.
	bool taaActive = false;
	if(TaaEnable && !bJustInitialised && type != MOTION_BLUR_SNIPER &&
	   EffectSwitch != POSTFX_OFF){
		RenderTAA(cam);
		taaActive = true;
	}
#endif

#ifdef POSTFX_FXAA
	if(FxaaEnable && !bJustInitialised && type != MOTION_BLUR_SNIPER &&
	   EffectSwitch != POSTFX_OFF
#ifdef POSTFX_HDR
	   && !taaActive
#endif
	  )
		RenderFXAA(cam);
#endif

	if(!bJustInitialised)
		RenderMotionBlur(cam, 175.0f * CMBlur::Drunkness);

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nil);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	if(NeedFrontBuffer(type)){
		RwRasterPushContext(pFrontBuffer);
		RwRasterRenderFast(RwCameraGetRaster(cam), 0, 0);
		RwRasterPopContext();
		bJustInitialised = false;
	}else
		bJustInitialised = true;

	POP_RENDERGROUP();
}

int CPostFX::PrevRed[NUMAVERAGE], CPostFX::AvgRed;
int CPostFX::PrevGreen[NUMAVERAGE], CPostFX::AvgGreen;
int CPostFX::PrevBlue[NUMAVERAGE], CPostFX::AvgBlue;
int CPostFX::PrevAlpha[NUMAVERAGE], CPostFX::AvgAlpha;
int CPostFX::Next;
int CPostFX::NumValues;

// This is rather annoying...the blur color can flicker slightly
// which becomes very visible when amplified by the shader
void
CPostFX::SmoothColor(uint32 red, uint32 green, uint32 blue, uint32 alpha)
{
	PrevRed[Next] = red;
	PrevGreen[Next] = green;
	PrevBlue[Next] = blue;
	PrevAlpha[Next] = alpha;
	Next = (Next+1) % NUMAVERAGE;
	NumValues = Min(NumValues+1, NUMAVERAGE);

	AvgRed = 0;
	AvgGreen = 0;
	AvgBlue = 0;
	AvgAlpha = 0;
	for(int i = 0; i < NumValues; i++){
		AvgRed += PrevRed[i];
		AvgGreen += PrevGreen[i];
		AvgBlue += PrevBlue[i];
		AvgAlpha += PrevAlpha[i];
	}
	AvgRed /= NumValues;
	AvgGreen /= NumValues;
	AvgBlue /= NumValues;
	AvgAlpha /= NumValues;
}

#endif
