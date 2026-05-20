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
#include "Timer.h"	// CTimer::GetTimeStepNonClipped — DoF focus crossfade animator
#include "Timecycle.h"
#include "Weather.h"	// CWeather::LightningFlash, Rain, WetRoads
#include "WaterLevel.h"	// CWaterLevel::GetWaterLevelNoWaves (underwater fog detection)
#include "Lights.h"	// pDirect (sun light) + DirectionalLightColourForFrame
#include "PointLights.h"	// CPointLights::aLights — volumetric in-scatter sources
#include "Camera.h"
#include "postfx.h"
#include "ibl.h"	// CIBL::Enabled / irradianceCube for the Phase 2 receiver
#include "spotShadow.h"	// CSpotShadow::Open/Close — RT lifecycle
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
// ACES expects linear HDR input — which is exactly what pHdrScene holds.
// With the full HDR pipeline online we run ACES by default; it bakes a
// sRGB-ish rolloff into the tonemap so the gamma toggle becomes redundant
// (and double-encoding it crushes shadows: the "HDR but dark as night"
// regression).
//
// Exposure 1.2 (down from the initial 1.6 attempt) — a gentler bump from
// the legacy 1.0 baseline. 1.6 + ACES + the new col*=exposure path (which
// was previously missing) was over-amplifying any HDR-range particles or
// alpha-accumulation residue in pHdrScene, producing the visible "HDR
// noise" symptom on dense scenes. 1.2 keeps the brightness lift without
// pushing the highlights deep into the ACES shoulder.
bool CPostFX::TonemapACES = true;
bool CPostFX::TonemapGamma = false;
float CPostFX::Exposure = 1.2f;
float CPostFX::Saturation = 1.05f;	// slight pop — picture had read flat at 1.0
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
int CPostFX::SsaoAlgorithm = 0;	// default to classic SSAO; alt algorithms opt-in
int CPostFX::SsaoMixMode = 1;	// weighted average is the friendliest default
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
int CPostFX::SsrStepCount = 28;	// bumped from 18 — binary refinement makes the extra steps cheap
float CPostFX::SsrThickness = 0.5f;
float CPostFX::SsrStrength = 0.6f;
float CPostFX::SsrFresnelBias = 0.04f;
// SSR miss fallback strength — 0 = black miss (legacy), 1 = full IBL sky.
// Default to 0.6 so off-screen reflections still get a plausible sky
// without overpowering proper hit colours.
float CPostFX::SsrSkyFallback = 0.6f;
// Screen-Space Global Illumination — off by default until the player opts
// in. 4 directions × 6 steps = sparse but cheap; TAA averages out the
// resulting per-pixel noise. Tuning targets ~1ms at 1080p with TAA on.
RwRaster *CPostFX::pSsgiA;
bool CPostFX::SsgiEnable = false;
float CPostFX::SsgiStrength = 0.7f;
float CPostFX::SsgiMaxDistance = 18.0f;	// metres — local bounce only
int CPostFX::SsgiStepCount = 6;
float CPostFX::SsgiNdotLGate = 0.05f;
// Depth of field — off by default; defaults give a tasteful cinematic
// near/far blur centred on ~15m (typical car interior distance).
RwRaster *CPostFX::pDofScratch;
RwRaster *CPostFX::pMotionBlurScratch;
// Default OFF — the legacy frame-buffer blur stays default behaviour
// for compatibility. Enabling the MV blur replaces it cleanly via
// the menu toggle.
bool CPostFX::MotionVecBlurEnable = false;
float CPostFX::MotionVecBlurStrength = 0.5f;
float CPostFX::MotionVecBlurMaxRadius = 0.05f;
bool CPostFX::DofEnable = false;
// FocusDistance is the user-settable *target* the menu slider writes to.
// FocusDistanceSmoothed is what the shader actually reads — a per-frame
// lerp toward the target, so slider movements and camera-mode-driven
// retargets (when DofAutoFocus lands) crossfade smoothly instead of
// hard-snapping. Time constant ~0.5 s: visually instant but kills the
// abrupt blur pulse you'd otherwise get every menu tick.
float CPostFX::DofFocusDistance = 15.0f;
float CPostFX::DofFocusDistanceSmoothed = 15.0f;
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
// Default ON — cones are decoupled from the global fog toggle so headlights
// and muzzle flashes still produce visible volumetric in-scatter at night
// without paying for full-screen height-fog. Cheap (≈4 ALU per step per
// light slot) and gated per-slot by light radius at upload time.
bool CPostFX::VolSpotEnable = true;
// VolFog raymarch quality. The host pushes this into hdrResolve_PS as a
// uniform; the shader's [loop] reads it as the bound count. 12 was the
// legacy hardcoded value; 16 is the new default — quality bump that's
// visually noticeable on banding-prone gradients (sky toward sun).
int32 CPostFX::VolFogSteps = 16;
int8  CPostFX::VolFogStepsIndex = 2;	// 0..4 = 8/12/16/24/32 — default High (16)
// Wet puddles — default ON: the effect costs ~6 ALU + a [branch] on dry
// frames (zero strength → skips the whole block) so leaving it enabled
// has negligible perf cost, and the visual upgrade during rain is
// significant. Users can still toggle it off in menu.
bool CPostFX::PuddlesEnable = true;
// Underwater caustics — default ON. Cheap [branch] above water (skips the
// whole block when worldZ > waterLevel), and the visible bonus when the
// player goes diving / swims off a pier is significant.
bool CPostFX::CausticsEnable = true;
float CPostFX::CausticsStrength = 1.0f;
// Shoreline foam default ON. Cheap [branch] gate, only fires within a
// ~1m band above water on level ground. Adds noticeable life to beach
// scenes without any new RT or asset cost.
bool CPostFX::FoamEnable = true;
float CPostFX::FoamStrength = 1.0f;

void
CPostFX::VolFogStepsAfterChange(int8 before, int8 after)
{
	(void)before;
	static const int32 kStepsTable[5] = { 8, 12, 16, 24, 32 };
	int8 idx = after;
	if(idx < 0) idx = 0;
	if(idx > 4) idx = 4;
	VolFogSteps = kStepsTable[idx];
	// No reallocation needed — the uniform is read fresh each frame.
}
RwRaster *CPostFX::pTaaHistA;
RwRaster *CPostFX::pTaaHistB;
bool CPostFX::TaaEnable = false;	// opt-in (FXAA stays default)
float CPostFX::TaaBlend = 0.12f;
float CPostFX::TaaClamp = 1.0f;
int CPostFX::TaaFrameIdx = 0;
// AaMode 0..3 — drives which AA pass(es) run. Default 1 (FXAA only)
// keeps the old behaviour when settings.ini doesn't have the new key.
int CPostFX::AaMode = 1;
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
static void *hbao_PS;	// HBAO (SsaoAlgorithm = 2)
static void *aoMix_PS;	// mix shader (SsaoAlgorithm = 3 → combine SSAO+GTAO+HBAO)
static RwRaster *pSsaoMixC;	// third scratch RT for HBAO output during mix
static rw::Camera *ssaoMixCam;
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
static void *ssgi_PS;
static rw::Camera *ssgiCam;	// half-res RGBA16F SSGI bounce-light target
static void *dof_PS;
static rw::Camera *dofCam;	// full-res RGBA16F bokeh scratch
static void *motionBlur_PS;
static rw::Camera *motionBlurCam;
// Prev-frame view-proj cached at the END of RenderMotionVecBlur.
// Independent of TAA's cache so motion blur works standalone with TAA
// off. First-frame default = identity → MV = 0 → no blur, same look
// as previous frame which is exactly what we want.
static rw::RawMatrix sMbPrevViewProj = {
	{ 1, 0, 0 }, 0,
	{ 0, 1, 0 }, 0,
	{ 0, 0, 1 }, 0,
	{ 0, 0, 0 }, 1,
};
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

// Last-known camera dimensions, used by ResizeIfChanged() to detect Alt-Tab
// / full-screen toggle / resolution-change events. Updated every Open call
// so the first Open seeds them with the current camera size.
static int32 sLastCamW = 0;
static int32 sLastCamH = 0;

bool
CPostFX::ResizeIfChanged(RwCamera *cam)
{
	if(cam == nullptr || pFrontBuffer == nullptr)
		return false;
	int32 curW = RwRasterGetWidth(RwCameraGetRaster(cam));
	int32 curH = RwRasterGetHeight(RwCameraGetRaster(cam));
	if(curW == sLastCamW && curH == sLastCamH)
		return false;
	// Resolution change detected — drop all camera-sized RTs and re-Open.
	// Close() destroys everything (HDR scene + SSAO/SSR/Bloom/TAA/G-buf
	// pyramids); the next Open recreates them at the new dimensions. This
	// is the same recovery path that runs after Alt-Tab + device lost.
	Close();
	Open(cam);
	return true;
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

	// Stamp the camera dimensions so ResizeIfChanged sees a stable baseline.
	sLastCamW = RwRasterGetWidth(RwCameraGetRaster(cam));
	sLastCamH = RwRasterGetHeight(RwCameraGetRaster(cam));
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
	// Spot shadow map — shared 512² R32F + light camera. Allocated
	// here so the BindReceiver / RenderShadowMap path called from
	// main.cpp's frame loop sees valid resources.
	CSpotShadow::Open(cam);
	// Phase 2 IBL cubes — opens both source and irradiance cubes, runs
	// the first capture+convolve so the cube has content before the
	// first scene draw reads it.
	CIBL::Open(cam);

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
	// Third half-res RT — only used in Mixed AO mode (HBAO output before
	// the aoMix_PS compose). Allocated here so we don't pay the create
	// cost during gameplay if the user toggles modes.
	pSsaoMixC = RwRasterCreate(sw, sh, depth, rwRASTERTYPECAMERATEXTURE);
	ssaoMixCam = CreateBloomCam(pSsaoMixC);
	g_ssaoW = sw;
	g_ssaoH = sh;

	// SSR shares the half-res target size with SSAO. RGBA8 is enough — the
	// reflection signal is LDR-ish (it's already gone through the same path
	// hdrResolve does) so we tonemap before storing. Camera reuses the
	// existing helper so SSR draws into a real librw camera bound to the
	// raster.
	pSsrA = RwRasterCreate(sw, sh, depth, rwRASTERTYPECAMERATEXTURE);
	ssrCam = CreateBloomCam(pSsrA);

	// SSGI bounce-light target. Half-res (same sw×sh) and RGBA16F so we
	// can store linear HDR bounce radiance without clipping at 1.0 — sun-
	// lit asphalt easily hits 2-3× the LDR ceiling, and we want that
	// energy to drive the additive compose in hdrResolve_PS before the
	// tonemap. Alpha channel left unused; the receiver reads .rgb.
	{
		int32 ssgiFmt = (int32)rw::Raster::CAMERATEXTURE | (int32)rw::Raster::F16_RGBA;
		pSsgiA = RwRasterCreate(sw, sh, 0, ssgiFmt);
		ssgiCam = CreateBloomCam(pSsgiA);
	}

	// DoF scratch — full-res RGBA16F so the bokeh blur preserves HDR
	// brightness for the tonemap that follows. Same dimensions as
	// pHdrScene; we ping-pong (pHdrScene → pDofScratch → pHdrScene
	// via copy or rebind) inside RenderDoF.
	{
		int32 colorFmt = (int32)rw::Raster::CAMERATEXTURE | (int32)rw::Raster::F16_RGBA;
		pDofScratch = RwRasterCreate(cw, ch, 0, colorFmt);
		dofCam = CreateBloomCam(pDofScratch);
	}

	// Motion-vector blur scratch — pow2-sized (same as pBackBuffer) so
	// the existing Vertex[] full-screen quad maps 1:1 to it. RGBA8 is
	// enough since this pass runs post-tonemap on the LDR backbuffer.
	pMotionBlurScratch = RwRasterCreate(width, height, depth, rwRASTERTYPECAMERATEXTURE);
	motionBlurCam = CreateBloomCam(pMotionBlurScratch);

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
#include "shaders/obj/ssgi_PS.inc"
	ssgi_PS = rw::d3d::createPixelShader(ssgi_PS_cso);
	}
	{
#include "shaders/obj/dof_PS.inc"
	dof_PS = rw::d3d::createPixelShader(dof_PS_cso);
	}
	{
#include "shaders/obj/motionBlur_PS.inc"
	motionBlur_PS = rw::d3d::createPixelShader(motionBlur_PS_cso);
	}
	{
#include "shaders/obj/gtao_PS.inc"
	gtao_PS = rw::d3d::createPixelShader(gtao_PS_cso);
	}
	{
#include "shaders/obj/hbao_PS.inc"
	hbao_PS = rw::d3d::createPixelShader(hbao_PS_cso);
	}
	{
#include "shaders/obj/aoMix_PS.inc"
	aoMix_PS = rw::d3d::createPixelShader(aoMix_PS_cso);
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
	if(ssaoMixCam){ DestroyBloomCam(ssaoMixCam); ssaoMixCam = nil; }
	if(pSsaoMixC){ RwRasterDestroy(pSsaoMixC); pSsaoMixC = nil; }
	if(ssrCam){ DestroyBloomCam(ssrCam); ssrCam = nil; }
	if(pSsrA){ RwRasterDestroy(pSsrA); pSsrA = nil; }
	if(ssgiCam){ DestroyBloomCam(ssgiCam); ssgiCam = nil; }
	if(pSsgiA){ RwRasterDestroy(pSsgiA); pSsgiA = nil; }
	if(dofCam){ DestroyBloomCam(dofCam); dofCam = nil; }
	if(pDofScratch){ RwRasterDestroy(pDofScratch); pDofScratch = nil; }
	if(motionBlurCam){ DestroyBloomCam(motionBlurCam); motionBlurCam = nil; }
	if(pMotionBlurScratch){ RwRasterDestroy(pMotionBlurScratch); pMotionBlurScratch = nil; }
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
	CSpotShadow::Close();
	CIBL::Close();
	// Run ForceReset BEFORE Close so the D3D9 render-state bits get
	// restored while the device pointer is still alive. After Close,
	// CGBuffer::Open might be called again with stale COLORWRITEENABLE1
	// = 0 from a prior session if we skipped this step.
	CGBuffer::ForceReset();
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
	if(ssgi_PS){ rw::d3d::destroyPixelShader(ssgi_PS); ssgi_PS = nil; }
	if(dof_PS){ rw::d3d::destroyPixelShader(dof_PS); dof_PS = nil; }
	if(motionBlur_PS){ rw::d3d::destroyPixelShader(motionBlur_PS); motionBlur_PS = nil; }
	if(gtao_PS){ rw::d3d::destroyPixelShader(gtao_PS); gtao_PS = nil; }
	if(hbao_PS){ rw::d3d::destroyPixelShader(hbao_PS); hbao_PS = nil; }
	if(aoMix_PS){ rw::d3d::destroyPixelShader(aoMix_PS); aoMix_PS = nil; }
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
	// Phase 2 cube switch — when CIBL is enabled and its irradiance cube
	// is populated, the receiver shader samples that instead of the
	// hemisphere gradient. iblParams.z carries the flag.
	rw::d3d::setIblUseCube(CIBL::Enabled && CIBL::irradianceCube != nil);
	rw::d3d::setWetness(wetness, WetSurfacesDiffuse, WetSurfacesSpec, WetSurfacesPower);

	// Rain ripples — animated normal perturbation on wet up-facing
	// surfaces. Strength fades with CWeather::Rain so the shimmer kicks
	// in during active rain and fades out as the weather clears. Time
	// only accumulates while raining; otherwise the wave phase freezes
	// (no ripples on dried-out wet roads). Tile scale 0.5 = ~2m wave
	// period — looks plausible for a falling-rain disturbance pattern.
	static float sRainRippleTime = 0.0f;
	float rainNow = CWeather::Rain;
	if(rainNow > 0.01f){
		float dtSec = CTimer::GetTimeStepNonClipped() * (1.0f/50.0f);
		if(dtSec > 0.2f) dtSec = 0.2f;	// pause / hitch guard
		sRainRippleTime += dtSec * rainNow;
	}
	rw::d3d::setRainRipples(sRainRippleTime, rainNow, 0.5f);

	// Puddles — fade with the max of active rain and lingering WetRoads
	// so puddles persist briefly after a storm ends (matches the existing
	// wetness behaviour). Menu toggle hard-gates the whole effect.
	float puddleStrength = 0.0f;
	if(PuddlesEnable){
		float wet = (CWeather::WetRoads > CWeather::Rain
		                ? CWeather::WetRoads
		                : CWeather::Rain);
		if(wet < 0.0f) wet = 0.0f;
		if(wet > 1.0f) wet = 1.0f;
		puddleStrength = wet;
	}
	rw::d3d::setPuddles(puddleStrength, 0.08f);

	// Caustics — runs every frame but the shader [branch] skips the
	// block above-water. Time only accumulates while the effect is
	// enabled so a long boot-up doesn't desync the animation. Water
	// level is hardcoded to ~6 m (Vice City sea level) for now —
	// future work could query CWaterLevel for per-region surfaces.
	static float sCausticsTime = 0.0f;
	float causticsStr = 0.0f;
	if(CausticsEnable){
		float dtSec = CTimer::GetTimeStepNonClipped() * (1.0f/50.0f);
		if(dtSec > 0.2f) dtSec = 0.2f;
		sCausticsTime += dtSec;
		causticsStr = CausticsStrength;
	}
	rw::d3d::setCaustics(sCausticsTime, causticsStr, 6.0f);

	// Shoreline foam — separate time accumulator from caustics so they
	// drift independently and don't read as one big repeated pattern.
	// Same gate logic: only accumulate while the effect is enabled.
	static float sFoamTime = 0.0f;
	float foamStr = 0.0f;
	if(FoamEnable){
		float dtSec = CTimer::GetTimeStepNonClipped() * (1.0f/50.0f);
		if(dtSec > 0.2f) dtSec = 0.2f;
		sFoamTime += dtSec;
		foamStr = FoamStrength;
	}
	rw::d3d::setFoam(sFoamTime, foamStr);

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
		// Pick the AO algorithm. All algorithms share c10/c11 constants
		// (radius/bias/intensity/farClip + texel/noise). In Mixed mode
		// (algorithm 3) we run SSAO here, then GTAO into pSsaoB, then
		// HBAO into pSsaoMixC, and finally compose via aoMix_PS.
		void *aoShader = ssao_PS;
		if(SsaoAlgorithm == 1 && gtao_PS) aoShader = gtao_PS;
		else if(SsaoAlgorithm == 2 && hbao_PS) aoShader = hbao_PS;
		else if(SsaoAlgorithm == 3 && gtao_PS) aoShader = ssao_PS; // Mixed: start with SSAO
		rw::d3d::im2dOverridePS = aoShader;
#endif
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, SsaoVertex, 4, Index, 6);
	}
	RwCameraEndUpdate((RwCamera*)ssaoCamA);

#ifdef RW_D3D9
	// Mixed mode — render GTAO into pSsaoB and HBAO into pSsaoMixC, then
	// compose via aoMix_PS. The compose writes back to pSsaoA so the
	// downstream bilateral blur picks up the merged result.
	if(SsaoAlgorithm == 3 && gtao_PS && hbao_PS && aoMix_PS &&
	   pSsaoMixC != nil && ssaoMixCam != nil){
		// Pass 1b: GTAO → pSsaoB
		RwCameraBeginUpdate((RwCamera*)ssaoCamB);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CGBuffer::pGbufNormalDepth);
		{
			rw::d3d::im2dOverridePS = gtao_PS;
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, SsaoVertex, 4, Index, 6);
		}
		RwCameraEndUpdate((RwCamera*)ssaoCamB);

		// Pass 1c: HBAO → pSsaoMixC
		RwCameraBeginUpdate((RwCamera*)ssaoMixCam);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CGBuffer::pGbufNormalDepth);
		{
			rw::d3d::im2dOverridePS = hbao_PS;
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, SsaoVertex, 4, Index, 6);
		}
		RwCameraEndUpdate((RwCamera*)ssaoMixCam);

		// Compose: pSsaoA (SSAO) + pSsaoB (GTAO) + pSsaoMixC (HBAO) →
		// pSsaoA (in-place). Bind s0/s1/s2 explicitly.
		RwCameraBeginUpdate((RwCamera*)ssaoCamA);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pSsaoA);
		BindRasterToSampler(1, pSsaoB);
		BindRasterToSampler(2, pSsaoMixC);
		{
			float pp[4] = { 1.0f, 1.0f, 1.0f, (float)SsaoMixMode };
			rw::d3d::d3ddevice->SetPixelShaderConstantF(10, pp, 1);
			rw::d3d::im2dOverridePS = aoMix_PS;
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, SsaoVertex, 4, Index, 6);
		}
		BindRasterToSampler(1, nil);
		BindRasterToSampler(2, nil);
		RwCameraEndUpdate((RwCamera*)ssaoCamA);
	}
#endif

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

// Stage 33: Screen-Space Global Illumination. Half-res 1-bounce gathering
// pass that piggybacks on the existing G-buffer (depth + world normal) +
// pHdrScene (scene radiance) + pSsaoA (GTAO bent normal in .gba). For each
// pixel: trace 4 hemispherical directions × ≤6 march steps; on hit, fetch
// HDR radiance and accumulate weighted by NdotL and distance falloff.
// Output lands in pSsgiA (RGBA16F linear) and is composed additively by
// hdrResolve_PS. Sparse on purpose — TAA averages the per-pixel jitter.
void
CPostFX::RenderSSGI(RwCamera *cam)
{
	if(!CGBuffer::HdrEnabled || !CGBuffer::GbufEnabled || !SsgiEnable)
		return;
	if(CGBuffer::pGbufNormalDepth == nil || CGBuffer::pHdrScene == nil ||
	   ssgi_PS == nil || pSsgiA == nil || ssgiCam == nil)
		return;

	PUSH_RENDERGROUP("CPostFX::RenderSSGI");

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

	RwCameraEndUpdate(cam);
	RwCameraBeginUpdate((RwCamera*)ssgiCam);
	// s0 = gbuf (normal+depth), s1 = HDR scene radiance, s2 = bent normal
	// hint from GTAO (.gba) — SSAO/HBAO fall back to surface N so the
	// shader's lerp toward "effectiveN" is a no-op when GTAO isn't active.
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CGBuffer::pGbufNormalDepth);
	BindRasterToSampler(1, CGBuffer::pHdrScene);
	if(pSsaoA != nil)
		BindRasterToSampler(2, pSsaoA);

#ifdef RW_D3D9
	{
		rw::Camera *rwcam = (rw::Camera*)cam;
		rw::V3d camPos = rwcam->getFrame()->getLTM()->pos;
		float farClip = rwcam->farPlane;

		// c10: params — strength is uploaded as the master gate so the
		// shader's early-out (ssgiParams.x < 0.001) bypasses the whole
		// 4-direction loop on disabled frames. step count clamped 3..8
		// inside the shader as belt-and-braces.
		float pp[4] = {
			SsgiStrength,
			SsgiMaxDistance,
			(float)SsgiStepCount,
			SsgiNdotLGate,
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, pp, 1);

		// c11: camera + farClip (mirror of volCamera / ssrViewer pattern).
		float cCam[4] = { camPos.x, camPos.y, camPos.z, farClip };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(11, cCam, 1);

		// c12..c15: frustum corner rays (TL, TR, BR, BL) — same layout
		// as the volumetric fog / SSR shaders so the reconstruction of
		// per-pixel world ray is identical.
		const rw::V3d *fc = rwcam->frustumCorners;
		float corners[4][4] = {
			{ fc[0].x, fc[0].y, fc[0].z, 0 },
			{ fc[1].x, fc[1].y, fc[1].z, 0 },
			{ fc[2].x, fc[2].y, fc[2].z, 0 },
			{ fc[3].x, fc[3].y, fc[3].z, 0 },
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(12, &corners[0][0], 4);

		// c16..c19: world-to-clip matrix (view × proj, row-major) so the
		// shader can project candidate hit points back to screen UV +
		// clip-Z for the depth-march comparison.
		rw::RawMatrix vp;
		rw::RawMatrix::mult(&vp, &rwcam->devView, &rwcam->devProj);
		rw::d3d::d3ddevice->SetPixelShaderConstantF(16, (const float*)&vp, 4);

		rw::d3d::im2dOverridePS = ssgi_PS;
	}
#endif

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, SsaoVertex, 4, Index, 6);

	RwCameraEndUpdate((RwCamera*)ssgiCam);
	BindRasterToSampler(1, nil);
	if(pSsaoA != nil)
		BindRasterToSampler(2, nil);
	RwCameraBeginUpdate(cam);

#ifdef RW_D3D9
	rw::d3d::im2dOverridePS = nil;
#endif

	POP_RENDERGROUP();
}

// Per-frame DoF focus distance animator. Exponential lerp toward the
// menu / script target with a ~0.5 s time constant — long enough that
// slider movements crossfade smoothly, short enough that a camera-mode
// retarget (third-person → bumper) reaches the new focus before the
// next pedestrian interaction. CTimer::ms_fTimeStep is in 50-Hz units
// (50 = 1 second) so the per-step factor falls out as ~0.04 per step;
// we cap min/max to keep the lerp stable when the game pauses or hitches.
static void
StepDofFocusAnimator(void)
{
	float dt = CTimer::GetTimeStepNonClipped() * (1.0f/50.0f);	// seconds
	if(dt < 0.001f) dt = 0.001f;
	if(dt > 0.2f)   dt = 0.2f;
	const float kTimeConstSec = 0.5f;
	float a = 1.0f - expf(-dt / kTimeConstSec);
	CPostFX::DofFocusDistanceSmoothed +=
		(CPostFX::DofFocusDistance - CPostFX::DofFocusDistanceSmoothed) * a;
}

void
CPostFX::RenderDoF(RwCamera *cam)
{
	// Step the focus animator each frame regardless of DofEnable so the
	// smoothed value tracks the target even while DoF is toggled off —
	// switching DoF on then back doesn't snap focus to a stale position.
	StepDofFocusAnimator();

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
		// Use the smoothed focus distance instead of the raw slider so
		// slider tweaks crossfade over ~0.5 s. The non-smoothed range
		// + aperture stay direct — those are quality knobs, not focus
		// targets, and shouldn't crossfade.
		float pp[4] = { farClip, DofFocusDistanceSmoothed, DofFocusRange, DofAperture };
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
CPostFX::RenderMotionVecBlur(RwCamera *cam)
{
	// Always update the prev-frame cache, even when the toggle is off,
	// so the first frame after toggling MotionVecBlurEnable on doesn't
	// see a stale (or identity) prev matrix that would smear half the
	// screen. Doing the update at the START of the function reads the
	// current frame's matrix as "prev for next frame" — same pattern
	// as TAA, just done unconditionally.
	rw::RawMatrix curViewProj;
	{
		rw::Camera *rwcam = (rw::Camera*)cam;
		rw::RawMatrix::mult(&curViewProj, &rwcam->devView, &rwcam->devProj);
	}

	if(!MotionVecBlurEnable || !CGBuffer::HdrEnabled || !CGBuffer::GbufEnabled){
		// Bookkeep the prev matrix anyway so the next frame's pass — if
		// the user just toggled enable — has a valid prev-VP to reproject.
		sMbPrevViewProj = curViewProj;
		return;
	}
	if(motionBlur_PS == nil || pMotionBlurScratch == nil || motionBlurCam == nil ||
	   CGBuffer::pGbufNormalDepth == nil){
		sMbPrevViewProj = curViewProj;
		return;
	}

	PUSH_RENDERGROUP("CPostFX::RenderMotionVecBlur");

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

	// Source: the current LDR backbuffer (post all other postfx).
	// Destination: pMotionBlurScratch.
	RwCameraEndUpdate(cam);
	RwCameraBeginUpdate((RwCamera*)motionBlurCam);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pBackBuffer);
	BindRasterToSampler(1, CGBuffer::pGbufNormalDepth);

#ifdef RW_D3D9
	{
		rw::Camera *rwcam = (rw::Camera*)cam;
		rw::V3d camPos = rwcam->getFrame()->getLTM()->pos;
		float farClip = rwcam->farPlane;

		// c10: blur params (.x = intensity, .y = max UV radius)
		float pp[4] = { MotionVecBlurStrength, MotionVecBlurMaxRadius, 8.0f, 0.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(10, pp, 1);

		// c11: camera + farClip
		float cCam[4] = { camPos.x, camPos.y, camPos.z, farClip };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(11, cCam, 1);

		// c12..c15: frustum corner rays
		const rw::V3d *fc = rwcam->frustumCorners;
		float corners[4][4] = {
			{ fc[0].x, fc[0].y, fc[0].z, 0 },
			{ fc[1].x, fc[1].y, fc[1].z, 0 },
			{ fc[2].x, fc[2].y, fc[2].z, 0 },
			{ fc[3].x, fc[3].y, fc[3].z, 0 },
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(12, &corners[0][0], 4);

		// c16..c19: previous-frame view-proj matrix (where each world
		// point landed on screen one frame ago).
		rw::d3d::d3ddevice->SetPixelShaderConstantF(16, (const float*)&sMbPrevViewProj, 4);

		rw::d3d::im2dOverridePS = motionBlur_PS;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Vertex, 4, Index, 6);
		rw::d3d::im2dOverridePS = nil;
	}
#endif

	BindRasterToSampler(1, nil);
	RwCameraEndUpdate((RwCamera*)motionBlurCam);

	// Copy pMotionBlurScratch back into pBackBuffer so subsequent passes
	// (legacy alpha overlay, frontbuffer capture) see the blurred image.
	RwCameraBeginUpdate(cam);
	RwRasterPushContext(pBackBuffer);
	RwRasterRenderFast(pMotionBlurScratch, 0, 0);
	RwRasterPopContext();

	// Bookkeep — next frame's reproject uses what was "current" this frame.
	sMbPrevViewProj = curViewProj;

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
	bool ssgiActive = SsgiEnable && CGBuffer::GbufEnabled && pSsgiA != nil;
	bool volFogActive = VolFogEnable && CGBuffer::GbufEnabled
	                 && CGBuffer::pGbufNormalDepth != nil;
	bool gbufNeeded = volFogActive || ssrActive;
	if(gbufNeeded)
		BindRasterToSampler(2, CGBuffer::pGbufNormalDepth);
	if(ssrActive)
		BindRasterToSampler(3, pSsrA);
	// SSGI compose — sampler s4 holds the half-res bounce-light buffer.
	// Bilinear filtered; bilerp at full-res adds an implicit smooth blur
	// that helps cover the sparse 4-direction sample noise.
	if(ssgiActive)
		BindRasterToSampler(4, pSsgiA);

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

		// c20: SSR compose strength + Fresnel F0 bias + sky fallback.
		// Wet weather scales the effective reflection strength linearly
		// with CWeather::WetRoads — wet asphalt becomes much more
		// mirror-like, dry asphalt keeps the user's normal SSR level.
		float wetBoost = 1.0f + 1.5f * (CWeather::WetRoads > CWeather::Rain
		                                  ? CWeather::WetRoads
		                                  : CWeather::Rain);
		float ssrStrengthLive = SsrStrength * wetBoost;
		if(ssrStrengthLive > 1.5f) ssrStrengthLive = 1.5f;	// clamp
		float ssrPar[4] = {
			ssrActive ? ssrStrengthLive : 0.0f,
			SsrFresnelBias,
			SsrSkyFallback * wetBoost,	// stronger sky bleed in rain too
			0.0f,
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(20, ssrPar, 1);

		// c21: camera world pos again (the SSR compose path needs it but
		// volCamera is already loaded; redundant write keeps the bindings
		// independent so we can drop volumetric without breaking SSR).
		float cView[4] = { camPos.x, camPos.y, camPos.z, 0.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(21, cView, 1);

		// c22..c24: mirror of the IBL sky/horizon/ground colours we
		// already uploaded to librw's c44..c46 for the receiver. The
		// postfx PS lives in its own constant address space, so we have
		// to push them again. Reuses CTimeCycle sky colours scaled by
		// CPostFX::IblExposure so the SSR fallback colour matches the
		// procedural ambient term used elsewhere in the frame.
		float skyT[4] = {
			(float)CTimeCycle::GetSkyTopRed()   * IblExposure,
			(float)CTimeCycle::GetSkyTopGreen() * IblExposure,
			(float)CTimeCycle::GetSkyTopBlue()  * IblExposure,
			0.0f,
		};
		float skyH[4] = {
			(float)CTimeCycle::GetSkyBottomRed()   * IblExposure,
			(float)CTimeCycle::GetSkyBottomGreen() * IblExposure,
			(float)CTimeCycle::GetSkyBottomBlue()  * IblExposure,
			0.0f,
		};
		float skyG[4] = { 0.06f, 0.055f, 0.05f, 0.0f };
		rw::d3d::d3ddevice->SetPixelShaderConstantF(22, skyT, 1);
		rw::d3d::d3ddevice->SetPixelShaderConstantF(23, skyH, 1);
		rw::d3d::d3ddevice->SetPixelShaderConstantF(24, skyG, 1);

		// c25..c32, c33..c40: volumetric spotlight slots — up to 8 in-
		// scatter sources for the volumetric ray-march to pick up
		// vehicle headlights, lamp posts, gunfire flashes, explosions.
		// We score every active CPointLights by (luminance × inverse-
		// distance²) relative to the camera and take the top 8. The
		// ray-march in hdrResolve_PS adds each light's contribution
		// per step weighted by density × inverse-square falloff, so
		// the scene reads with proper volumetric haze around bright
		// sources at night / in rain.
		//
		// Gating is independent of the global VolFog toggle — players
		// who don't want the full-screen height fog still get headlight
		// / muzzle-flash cones at night. The shader's per-light loop is
		// unrolled and masked by col.a so empty slots cost ~0 ALU.
		enum { VOL_SPOT_MAX = 8 };
		float volSpotPos[VOL_SPOT_MAX][4] = {
			{0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0},
			{0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0},
		};
		float volSpotCol[VOL_SPOT_MAX][4] = {
			{0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0},
			{0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0},
		};

		// Pick top-N by score (luminance / dist²) relative to camera.
		struct ScoredVL { int idx; float score; };
		ScoredVL pick[VOL_SPOT_MAX];
		for(int k = 0; k < VOL_SPOT_MAX; k++){ pick[k].idx = -1; pick[k].score = 0.0f; }
		CVector camV(camPos.x, camPos.y, camPos.z);
		for(int i = 0; i < CPointLights::NumLights; i++){
			const CRegisteredPointLight &L = CPointLights::aLights[i];
			if(L.type != CPointLights::LIGHT_POINT) continue;
			float lum = L.red + L.green + L.blue;
			if(lum < 0.05f) continue;
			CVector d = L.coors - camV;
			float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
			if(d2 < 0.01f) d2 = 0.01f;
			float score = lum / d2;
			// Insertion into the N-slot top-K by simple unrolled
			// comparison — picks the dimmest current slot and
			// replaces if our score is higher.
			int worst = 0;
			for(int k = 1; k < VOL_SPOT_MAX; k++)
				if(pick[k].score < pick[worst].score) worst = k;
			if(score > pick[worst].score){
				pick[worst].idx = i;
				pick[worst].score = score;
			}
		}

		// Pack the picked lights. radius squared in .w so the shader can
		// skip the sqrt; col.a is the master multiplier and ALSO the
		// per-slot enable: 0 = inactive slot (shader unrolls cheap),
		// 1 = active. Gated by the new CPostFX::VolSpotEnable toggle so
		// players can disable the cones without touching the global fog.
		const float spotMaster = VolSpotEnable ? 1.0f : 0.0f;
		for(int k = 0; k < VOL_SPOT_MAX; k++){
			if(pick[k].idx < 0) continue;
			const CRegisteredPointLight &L = CPointLights::aLights[pick[k].idx];
			if(L.radius < 0.1f) continue;	// degenerate / expired slot
			volSpotPos[k][0] = L.coors.x;
			volSpotPos[k][1] = L.coors.y;
			volSpotPos[k][2] = L.coors.z;
			volSpotPos[k][3] = L.radius * L.radius;
			volSpotCol[k][0] = L.red;
			volSpotCol[k][1] = L.green;
			volSpotCol[k][2] = L.blue;
			volSpotCol[k][3] = spotMaster;
		}

		rw::d3d::d3ddevice->SetPixelShaderConstantF(25, &volSpotPos[0][0], VOL_SPOT_MAX);
		rw::d3d::d3ddevice->SetPixelShaderConstantF(33, &volSpotCol[0][0], VOL_SPOT_MAX);

		// c41: volQuality.x = host-driven step count for the volumetric
		// raymarch. Allows the menu's "VolFog quality" tier (Low..Ultra)
		// to dial cost vs quality without a shader re-link. c25..c40 is
		// volSpot pos+col so we sit just after.
		float volQuality[4] = {
			(float)VolFogSteps,
			0.0f, 0.0f, 0.0f,
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(41, volQuality, 1);

		// c42: SSGI compose — .x = strength gate (0 = bypass the whole
		// additive block via [branch] in hdrResolve_PS). When SSGI is
		// disabled this stays at 0 so the shader path is a single ALU
		// comparison + skip; no sampler s4 fetch happens.
		float ssgiCompose[4] = {
			ssgiActive ? SsgiStrength : 0.0f,
			0.0f, 0.0f, 0.0f,
		};
		rw::d3d::d3ddevice->SetPixelShaderConstantF(42, ssgiCompose, 1);
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
	if(ssgiActive)
		BindRasterToSampler(4, nil);
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
	// AA mode drives both passes:
	//   1 (FXAA): RenderFXAA only
	//   2 (TAA):  RenderTAA only
	//   3 (Combined): RenderTAA then RenderFXAA — TAA stabilises the
	//      temporal signal first, FXAA finishes any geometric edges
	//      the neighbourhood clamp left aliased. Slightly more blur
	//      than TAA-alone but much cleaner static + motion shots.
	// 0 (Off): no AA. Legacy FxaaEnable/TaaEnable bools no longer
	// participate — AaMode is authoritative.
	if((AaMode == 2 || AaMode == 3) && !bJustInitialised &&
	   type != MOTION_BLUR_SNIPER && EffectSwitch != POSTFX_OFF){
		RenderTAA(cam);
	}
#endif

#ifdef POSTFX_FXAA
#ifdef POSTFX_HDR
	bool runFxaa = (AaMode == 1 || AaMode == 3);
#else
	bool runFxaa = FxaaEnable;
#endif
	if(runFxaa && !bJustInitialised && type != MOTION_BLUR_SNIPER &&
	   EffectSwitch != POSTFX_OFF)
		RenderFXAA(cam);
#endif

	// Motion-vector blur — runs BEFORE the legacy alpha-overlay so the
	// blurred image is what gets sampled into the frontbuffer for the
	// next-frame feedback path. Self-gates on MotionVecBlurEnable; safe
	// no-op when disabled (still updates the prev-VP cache so toggling
	// on mid-game doesn't smear).
	if(!bJustInitialised && type != MOTION_BLUR_SNIPER &&
	   EffectSwitch != POSTFX_OFF)
		RenderMotionVecBlur(cam);

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
