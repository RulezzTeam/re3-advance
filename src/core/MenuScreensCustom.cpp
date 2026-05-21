#include "common.h"
#if defined DETECT_JOYSTICK_MENU && defined XINPUT
#include <windows.h>
#include <xinput.h>
#if !defined(PSAPI_VERSION) || (PSAPI_VERSION > 1)
#pragma comment( lib, "Xinput9_1_0.lib" )
#else
#pragma comment( lib, "Xinput.lib" )
#endif
#endif
#include "platform.h"
#include "crossplatform.h"
#include "Renderer.h"
#include "Frontend.h"
#include "Font.h"
#include "Camera.h"
#include "main.h"
#include "MBlur.h"
#include "postfx.h"
#ifdef POSTFX_HDR
#include "gbuffer.h"
#include "ibl.h"
#include "dynamicLights.h"
#endif
#ifdef POSTFX_WATER_REFLECTION
#include "waterReflection.h"
#endif
#ifdef POSTFX_CSM
#include "csm.h"
#endif
#ifdef POSTFX_HDR
#include "spotShadow.h"
#include "decals.h"	// Stage 15 — CDecals menu binding
#endif
#include "custompipes.h"
#include "RwHelper.h"
#include "Text.h"
#include "Streaming.h"
#include "FileLoader.h"
#include "Collision.h"
#include "ModelInfo.h"
#include "Pad.h"
#include "ControllerConfig.h"
#include "DMAudio.h"
#include "IniFile.h"
#include "CarCtrl.h"
#include "Population.h"

// Menu screens array is at the bottom of the file.

#ifdef PC_MENU

#ifdef CUSTOM_FRONTEND_OPTIONS

#if defined(IMPROVED_VIDEOMODE) && !defined(GTA_HANDHELD)
	#define VIDEOMODE_SELECTOR MENUACTION_CFO_SELECT, "FEM_SCF", { new CCFOSelect((int8*)&FrontEndMenuManager.m_nPrefsWindowed, "VideoMode", "Windowed", screenModes, 2, true, ScreenModeAfterChange, true) }, 0, 0, MENUALIGN_LEFT,
#else
	#define VIDEOMODE_SELECTOR
#endif

#ifdef MULTISAMPLING
	#define MULTISAMPLING_SELECTOR MENUACTION_CFO_DYNAMIC, "FED_AAS", { new CCFODynamic((int8*)&FrontEndMenuManager.m_nPrefsMSAALevel, "Graphics", "MultiSampling", MultiSamplingDraw, MultiSamplingButtonPress) }, 0, 0, MENUALIGN_LEFT,
#else
	#define MULTISAMPLING_SELECTOR
#endif

#ifdef CUTSCENE_BORDERS_SWITCH
	#define CUTSCENE_BORDERS_TOGGLE MENUACTION_CFO_SELECT, "FEM_CSB", { new CCFOSelect((int8 *)&FrontEndMenuManager.m_PrefsCutsceneBorders, "Display", "CutsceneBorders", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT,
#else
	#define CUTSCENE_BORDERS_TOGGLE
#endif

#ifdef FREE_CAM
	#define FREE_CAM_TOGGLE MENUACTION_CFO_SELECT, "FEC_FRC", { new CCFOSelect((int8*)&TheCamera.bFreeCam, "Display", "FreeCam", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT,
#else
	#define FREE_CAM_TOGGLE
#endif

#ifdef PS2_ALPHA_TEST
	#define DUALPASS_SELECTOR MENUACTION_CFO_SELECT, "FEM_2PR", { new CCFOSelect((int8*)&gPS2alphaTest, "Graphics", "PS2AlphaTest", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT,
#else
	#define DUALPASS_SELECTOR 
#endif

#ifdef PED_CAR_DENSITY_SLIDERS
	// 0.2f - 3.4f makes it possible to have 1.0f somewhere inbetween
	#define DENSITY_SLIDERS \
		MENUACTION_CFO_SLIDER, "FEM_PED", { new CCFOSlider(&CIniFile::PedNumberMultiplier, "Display", "PedDensity", 0.2f, 300.4f, PedDensityChange) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FEM_CAR", { new CCFOSlider(&CIniFile::CarNumberMultiplier, "Display", "CarDensity", 0.2f, 300.4f, CarDensityChange) }, 0, 0, MENUALIGN_LEFT, 
#else
	#define DENSITY_SLIDERS 
#endif

#ifdef NO_ISLAND_LOADING
	#define ISLAND_LOADING_SELECTOR MENUACTION_CFO_SELECT, "FEM_ISL", { new CCFOSelect((int8*)&FrontEndMenuManager.m_PrefsIslandLoading, "Graphics", "IslandLoading", islandLoadingOpts, ARRAY_SIZE(islandLoadingOpts), true, IslandLoadingAfterChange) }, 0, 0, MENUALIGN_LEFT,
#else
	#define ISLAND_LOADING_SELECTOR 
#endif

#ifdef EXTENDED_COLOURFILTER
	#define POSTFX_SELECTORS \
		MENUACTION_CFO_SELECT, "FED_CLF", { new CCFOSelect((int8*)&CPostFX::EffectSwitch, "Graphics", "ColourFilter", filterNames, ARRAY_SIZE(filterNames), false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_MBL", { new CCFOSelect((int8*)&CPostFX::MotionBlurOn, "Graphics", "MotionBlur", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT,
#else
	#define POSTFX_SELECTORS
#endif

// Advanced postfx selectors. Each block is gated behind its compile-time
// flag so platforms without the corresponding shaders just compile them out.
#ifdef POSTFX_BLOOM
	#define POSTFX_BLOOM_SELECTORS \
		MENUACTION_CFO_SELECT, "FED_BLM", { new CCFOSelect((int8*)&CPostFX::BloomEnable, "Graphics", "Bloom", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_BLT", { new CCFOSlider(&CPostFX::BloomThreshold, "Graphics", "BloomThreshold", 0.0f, 2.0f) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_BLI", { new CCFOSlider(&CPostFX::BloomIntensity, "Graphics", "BloomIntensity", 0.0f, 2.0f) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_BLS", { new CCFOSlider(&CPostFX::BloomSaturation, "Graphics", "BloomSaturation", 0.0f, 3.0f) }, 0, 0, MENUALIGN_LEFT,
#else
	#define POSTFX_BLOOM_SELECTORS
#endif

#ifdef POSTFX_TONEMAP
	#define POSTFX_TONEMAP_SELECTORS \
		MENUACTION_CFO_SELECT, "FED_ACS", { new CCFOSelect((int8*)&CPostFX::TonemapACES, "Graphics", "TonemapACES", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_GAM", { new CCFOSelect((int8*)&CPostFX::TonemapGamma, "Graphics", "TonemapGamma", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_EXP", { new CCFOSlider(&CPostFX::Exposure, "Graphics", "Exposure", 0.25f, 3.0f) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_SAT", { new CCFOSlider(&CPostFX::Saturation, "Graphics", "Saturation", 0.0f, 2.0f) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_VGI", { new CCFOSlider(&CPostFX::VignetteIntensity, "Graphics", "VignetteIntensity", 0.0f, 1.0f) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_VGS", { new CCFOSlider(&CPostFX::VignetteSoftness, "Graphics", "VignetteSoftness", 0.0f, 1.0f) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_CAS", { new CCFOSlider(&CPostFX::CAStrength, "Graphics", "ChromaticAberration", 0.0f, 0.02f) }, 0, 0, MENUALIGN_LEFT,
#else
	#define POSTFX_TONEMAP_SELECTORS
#endif

#ifdef POSTFX_FXAA
	#define POSTFX_FXAA_SELECTORS \
		MENUACTION_CFO_SELECT, "FED_FXA", { new CCFOSelect((int8*)&CPostFX::FxaaEnable, "Graphics", "FXAA", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_FXS", { new CCFOSlider(&CPostFX::FxaaStrength, "Graphics", "FXAAStrength", 0.0f, 1.0f) }, 0, 0, MENUALIGN_LEFT,
#else
	#define POSTFX_FXAA_SELECTORS
#endif

#ifdef POSTFX_HDR
	// AaMode 0 = Off, 1 = FXAA, 2 = TAA, 3 = TAA+FXAA. TaaBlend matters
	// only for modes 2/3, but the dependency hook here takes a single
	// int8* — so we let TaaBlend gate on AaMode != 0 (any AA active).
	// The AaMode selector itself + MotionVecBlur gate on HDR being on
	// since both run inside the HDR resolve pipeline. MotionVecBlur
	// strength gates on the toggle itself.
	#define POSTFX_TAA_SELECTORS \
		MENUACTION_CFO_SELECT, "FED_AAM", { new CCFOSelect((int8*)&CPostFX::AaMode, "Graphics", "AaMode", aaModeNames, 4, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_TAB", { new CCFOSlider(&CPostFX::TaaBlend, "Graphics", "TaaBlend", 0.02f, 0.5f, nil, (int8*)&CPostFX::AaMode, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_MVB", { new CCFOSelect((int8*)&CPostFX::MotionVecBlurEnable, "Graphics", "MotionVecBlur", off_on, 2, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_MBS", { new CCFOSlider(&CPostFX::MotionVecBlurStrength, "Graphics", "MotionVecBlurStrength", 0.0f, 1.0f, nil, (int8*)&CPostFX::MotionVecBlurEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT,
#else
	#define POSTFX_TAA_SELECTORS
#endif

#ifdef POSTFX_GODRAYS
	#define POSTFX_GODRAYS_SELECTORS \
		MENUACTION_CFO_SELECT, "FED_GDR", { new CCFOSelect((int8*)&CPostFX::GodRaysEnable, "Graphics", "GodRays", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_GDE", { new CCFOSlider(&CPostFX::GodRaysExposure, "Graphics", "GodRaysExposure", 0.0f, 2.0f) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_GDD", { new CCFOSlider(&CPostFX::GodRaysDensity, "Graphics", "GodRaysDensity", 0.3f, 1.5f) }, 0, 0, MENUALIGN_LEFT,
#else
	#define POSTFX_GODRAYS_SELECTORS
#endif

#ifdef SOFT_SHADOWS
	#define POSTFX_SHADOWS_SELECTORS \
		MENUACTION_CFO_SLIDER, "FED_PCR", { new CCFOSlider(&shadowPCFRadius, "Graphics", "ShadowPCFRadius", 0.5f, 3.5f) }, 0, 0, MENUALIGN_LEFT,
#else
	#define POSTFX_SHADOWS_SELECTORS
#endif

#ifdef MULTI_ENVMAP
	#define POSTFX_ENVMAP_SELECTORS \
		MENUACTION_CFO_SELECT, "FED_EMR", { new CCFOSelect(&CustomPipes::EnvMapSizeIndex, "Graphics", "EnvMapSize", envMapSizes, ARRAY_SIZE(envMapSizes), false, CustomPipes::EnvMapSizeAfterChange) }, 0, 0, MENUALIGN_LEFT,
#else
	#define POSTFX_ENVMAP_SELECTORS
#endif

#ifdef POSTFX_HDR
	// Cascading dependency rules. Each sub-option declares the FULL
	// chain of toggles it needs — immediate parent first, then any
	// transitive prerequisite. Walks via CCFODepsActive in Frontend.cpp,
	// so e.g. SsaoStrength (gates on SsaoEnable + HdrEnabled) goes gray
	// even when SsaoEnable is still 1 in memory but HDR is off.
	//
	// Two pointer slots is enough for the natural chain depth here:
	// [sub-option's parent toggle] → [HDR master]. Effects whose parent
	// IS the HDR master (SSAO/SSR/DoF/VolFog/MotionVecBlur toggles) only
	// need slot 1.
	#define POSTFX_HDR_SELECTORS \
		MENUACTION_CFO_SELECT, "FED_HDR", { new CCFOSelect((int8*)&CGBuffer::HdrEnabled, "Graphics", "HDR", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_GBF", { new CCFOSelect((int8*)&CGBuffer::GbufEnabled, "Graphics", "GBuffer", off_on, 2, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_SAO", { new CCFOSelect((int8*)&CPostFX::SsaoEnable, "Graphics", "SSAO", off_on, 2, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_SAA", { new CCFOSelect((int8*)&CPostFX::SsaoAlgorithm, "Graphics", "SsaoAlgo", ssaoAlgoNames, 4, false, nil, false, (int8*)&CPostFX::SsaoEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_SAM", { new CCFOSelect((int8*)&CPostFX::SsaoMixMode, "Graphics", "SsaoMix", ssaoMixNames, 3, false, nil, false, (int8*)&CPostFX::SsaoEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_SAR", { new CCFOSlider(&CPostFX::SsaoRadius, "Graphics", "SsaoRadius", 0.2f, 3.0f, nil, (int8*)&CPostFX::SsaoEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_SAI", { new CCFOSlider(&CPostFX::SsaoIntensity, "Graphics", "SsaoIntensity", 0.0f, 4.0f, nil, (int8*)&CPostFX::SsaoEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_SAS", { new CCFOSlider(&CPostFX::SsaoStrength, "Graphics", "SsaoStrength", 0.0f, 1.0f, nil, (int8*)&CPostFX::SsaoEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_CAO", { new CCFOSlider(&CPostFX::SsaoContactStrength, "Graphics", "SsaoContact", 0.0f, 1.5f, nil, (int8*)&CPostFX::SsaoEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_CAR", { new CCFOSlider(&CPostFX::SsaoContactRadius, "Graphics", "SsaoContactRadius", 1.0f, 8.0f, nil, (int8*)&CPostFX::SsaoEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_CSS", { new CCFOSlider(&CPostFX::ContactShadowStrength, "Graphics", "ContactShadow", 0.0f, 1.5f, nil, (int8*)&CPostFX::SsaoEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_CST", { new CCFOSlider(&CPostFX::ContactShadowThickness, "Graphics", "ContactShadowThick", 0.3f, 4.0f, nil, (int8*)&CPostFX::SsaoEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_IBL", { new CCFOSelect((int8*)&CPostFX::IblEnabled, "Graphics", "IBL", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_IBI", { new CCFOSlider(&CPostFX::IblIntensity, "Graphics", "IblIntensity", 0.0f, 2.5f, nil, (int8*)&CPostFX::IblEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_IBH", { new CCFOSlider(&CPostFX::IblHorizonExp, "Graphics", "IblHorizonExp", 0.5f, 6.0f, nil, (int8*)&CPostFX::IblEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_IBG", { new CCFOSlider(&CPostFX::IblGroundTint, "Graphics", "IblGroundTint", 0.0f, 1.0f, nil, (int8*)&CPostFX::IblEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_IBC", { new CCFOSelect((int8*)&CIBL::Enabled, "Graphics", "IblCube", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_IBR", { new CCFOSlider(&CIBL::ReflStrength, "Graphics", "IblRefl", 0.0f, 3.0f, nil, (int8*)&CIBL::Enabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_ICS", { new CCFOSelect(&CIBL::CaptureSizeIndex, "Graphics", "IblCubeSize", iblCubeCaptureSizes, 4, false, CIBL::CaptureSizeAfterChange, false, (int8*)&CIBL::Enabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_IIS", { new CCFOSelect(&CIBL::IrradianceSizeIndex, "Graphics", "IblIrrSize", iblIrradianceSizes, 4, false, CIBL::IrradianceSizeAfterChange, false, (int8*)&CIBL::Enabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_DPL", { new CCFOSelect((int8*)&CDynamicLights::Enabled, "Graphics", "DynLights", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_DPI", { new CCFOSlider(&CDynamicLights::Intensity, "Graphics", "DynLightInt", 0.0f, 3.0f, nil, (int8*)&CDynamicLights::Enabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_DPR", { new CCFOSlider(&CDynamicLights::Reach, "Graphics", "DynLightReach", 0.5f, 4.0f, nil, (int8*)&CDynamicLights::Enabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_SSR", { new CCFOSelect((int8*)&CPostFX::SsrEnable, "Graphics", "SSR", off_on, 2, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_SSS", { new CCFOSlider(&CPostFX::SsrStrength, "Graphics", "SsrStrength", 0.0f, 1.0f, nil, (int8*)&CPostFX::SsrEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_SSD", { new CCFOSlider(&CPostFX::SsrMaxDistance, "Graphics", "SsrDistance", 5.0f, 120.0f, nil, (int8*)&CPostFX::SsrEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_SST", { new CCFOSlider(&CPostFX::SsrThickness, "Graphics", "SsrThickness", 0.1f, 3.0f, nil, (int8*)&CPostFX::SsrEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_SSF", { new CCFOSlider(&CPostFX::SsrFresnelBias, "Graphics", "SsrFresnel", 0.0f, 1.0f, nil, (int8*)&CPostFX::SsrEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_SSK", { new CCFOSlider(&CPostFX::SsrSkyFallback, "Graphics", "SsrSkyFallback", 0.0f, 1.5f, nil, (int8*)&CPostFX::SsrEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_SGI", { new CCFOSelect((int8*)&CPostFX::SsgiEnable, "Graphics", "SSGI", off_on, 2, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_SGS", { new CCFOSlider(&CPostFX::SsgiStrength, "Graphics", "SsgiStrength", 0.0f, 2.0f, nil, (int8*)&CPostFX::SsgiEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_SGD", { new CCFOSlider(&CPostFX::SsgiMaxDistance, "Graphics", "SsgiDistance", 4.0f, 60.0f, nil, (int8*)&CPostFX::SsgiEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_BNA", { new CCFOSelect((int8*)&CPostFX::BentNormalAmbientEnable, "Graphics", "BentN", off_on, 2, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_BNS", { new CCFOSlider(&CPostFX::BentNormalAmbientStrength, "Graphics", "BentNStrength", 0.0f, 2.0f, nil, (int8*)&CPostFX::BentNormalAmbientEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_BNB", { new CCFOSlider(&CPostFX::BentNormalAmbientBias, "Graphics", "BentNBias", 0.0f, 1.0f, nil, (int8*)&CPostFX::BentNormalAmbientEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_SHP", { new CCFOSelect((int8*)&CPostFX::ShProbeEnable, "Graphics", "SHProbes", off_on, 2, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_SHS", { new CCFOSlider(&CPostFX::ShProbeStrength, "Graphics", "SHStrength", 0.0f, 2.0f, nil, (int8*)&CPostFX::ShProbeEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_OCP", { new CCFOSelect((int8*)&CPostFX::OcclusionProbeEnable, "Graphics", "OccProbes", off_on, 2, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_OCS", { new CCFOSlider(&CPostFX::OcclusionProbeStrength, "Graphics", "OccStrength", 0.0f, 1.0f, nil, (int8*)&CPostFX::OcclusionProbeEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_BNP", { new CCFOSlider(&CPostFX::BentNormalProbeBias, "Graphics", "BentNProbe", 0.0f, 1.0f, nil, (int8*)&CPostFX::OcclusionProbeEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_ATP", { new CCFOSelect((int8*)&CPostFX::AtmosphereProbeEnable, "Graphics", "AtmoProbes", off_on, 2, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_ATS", { new CCFOSlider(&CPostFX::AtmosphereProbeStrength, "Graphics", "AtmoStrength", 0.0f, 1.0f, nil, (int8*)&CPostFX::AtmosphereProbeEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_RFP", { new CCFOSelect((int8*)&CPostFX::ReflectionProbeEnable, "Graphics", "ReflProbes", off_on, 2, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_RFS", { new CCFOSlider(&CPostFX::ReflectionProbeStrength, "Graphics", "ReflStrength", 0.0f, 1.0f, nil, (int8*)&CPostFX::ReflectionProbeEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_DCL", { new CCFOSelect((int8*)&CDecals::Enabled, "Graphics", "Decals", off_on, 2, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_DCA", { new CCFOSlider(&CDecals::MaxAgeSeconds, "Graphics", "DecalAge", 5.0f, 120.0f, nil, (int8*)&CDecals::Enabled, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_WET", { new CCFOSelect((int8*)&CPostFX::WetSurfacesEnable, "Graphics", "WetSurfaces", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_WTI", { new CCFOSlider(&CPostFX::WetSurfacesIntensity, "Graphics", "WetIntensity", 0.0f, 2.0f, nil, (int8*)&CPostFX::WetSurfacesEnable) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_WTS", { new CCFOSlider(&CPostFX::WetSurfacesSpec, "Graphics", "WetSpec", 1.0f, 6.0f, nil, (int8*)&CPostFX::WetSurfacesEnable) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_PUD", { new CCFOSelect((int8*)&CPostFX::PuddlesEnable, "Graphics", "Puddles", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_CAU", { new CCFOSelect((int8*)&CPostFX::CausticsEnable, "Graphics", "Caustics", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_CAS", { new CCFOSlider(&CPostFX::CausticsStrength, "Graphics", "CausticsStrength", 0.0f, 2.0f, nil, (int8*)&CPostFX::CausticsEnable) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_FOA", { new CCFOSelect((int8*)&CPostFX::FoamEnable, "Graphics", "ShoreFoam", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_FAS", { new CCFOSlider(&CPostFX::FoamStrength, "Graphics", "ShoreFoamStrength", 0.0f, 2.0f, nil, (int8*)&CPostFX::FoamEnable) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_DOF", { new CCFOSelect((int8*)&CPostFX::DofEnable, "Graphics", "DoF", off_on, 2, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_DFD", { new CCFOSlider(&CPostFX::DofFocusDistance, "Graphics", "DofFocus", 1.0f, 80.0f, nil, (int8*)&CPostFX::DofEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_DFR", { new CCFOSlider(&CPostFX::DofFocusRange, "Graphics", "DofRange", 0.5f, 30.0f, nil, (int8*)&CPostFX::DofEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_DFA", { new CCFOSlider(&CPostFX::DofAperture, "Graphics", "DofAperture", 0.0f, 0.04f, nil, (int8*)&CPostFX::DofEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_VOL", { new CCFOSelect((int8*)&CPostFX::VolFogEnable, "Graphics", "VolFog", off_on, 2, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_VFS", { new CCFOSlider(&CPostFX::VolFogStrength, "Graphics", "VolFogStrength", 0.0f, 1.0f, nil, (int8*)&CPostFX::VolFogEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_VFD", { new CCFOSlider(&CPostFX::VolFogDensity, "Graphics", "VolFogDensity", 0.0f, 0.08f, nil, (int8*)&CPostFX::VolFogEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_VFH", { new CCFOSlider(&CPostFX::VolFogHeightFalloff, "Graphics", "VolFogHeight", 0.0f, 0.1f, nil, (int8*)&CPostFX::VolFogEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_VFG", { new CCFOSlider(&CPostFX::VolFogHG, "Graphics", "VolFogHG", 0.0f, 0.95f, nil, (int8*)&CPostFX::VolFogEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_VFB", { new CCFOSlider(&CPostFX::VolFogSunBoost, "Graphics", "VolFogSunBoost", 0.0f, 4.0f, nil, (int8*)&CPostFX::VolFogEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_VFQ", { new CCFOSelect(&CPostFX::VolFogStepsIndex, "Graphics", "VolFogSteps", volFogStepCounts, 5, false, CPostFX::VolFogStepsAfterChange, false, (int8*)&CPostFX::VolFogEnable, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_VSP", { new CCFOSelect((int8*)&CPostFX::VolSpotEnable, "Graphics", "VolSpot", off_on, 2, false, nil, false, (int8*)&CGBuffer::HdrEnabled) }, 0, 0, MENUALIGN_LEFT,
#else
	#define POSTFX_HDR_SELECTORS
#endif

#ifdef POSTFX_WATER_REFLECTION
	#define POSTFX_WATER_REFLECTION_SELECTORS \
		MENUACTION_CFO_SELECT, "FED_WRR", { new CCFOSelect((int8*)&CWaterReflection::Enabled, "Graphics", "WaterReflection", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_WRZ", { new CCFOSelect(&CWaterReflection::ResolutionIndex, "Graphics", "WaterReflectionSize", waterReflectionSizes, 4, false, CWaterReflection::ResolutionAfterChange, false, (int8*)&CWaterReflection::Enabled) }, 0, 0, MENUALIGN_LEFT,
#else
	#define POSTFX_WATER_REFLECTION_SELECTORS
#endif

#ifdef POSTFX_CSM
	// CSM sub-options gate on CCSM::Enabled; SpotShadow sub-options
	// gate on CSpotShadow::Enabled. Both run inside default_pp_PS so
	// they ALSO implicitly need HDR/GBuf, but the immediate-parent gate
	// is more useful UX-wise — flipping the parent off makes its own
	// sub-options gray, which is what the user expects.
	#define POSTFX_CSM_SELECTORS \
		MENUACTION_CFO_SELECT, "FED_CSM", { new CCFOSelect((int8*)&CCSM::Enabled, "Graphics", "CSM", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_CSS", { new CCFOSlider(&CCSM::Strength, "Graphics", "CSMStrength", 0.0f, 1.0f, nil, (int8*)&CCSM::Enabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_CMR", { new CCFOSelect(&CCSM::MapSizeIndex, "Graphics", "CSMMapSize", cascadeMapSizes, 3, false, CCSM::MapSizeAfterChange, false, (int8*)&CCSM::Enabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_CFM", { new CCFOSelect((int8*)&CCSM::SoftnessMode, "Graphics", "CSMSoft", csmSoftNames, 7, false, CCSM::SoftnessModeAfterChange, false, (int8*)&CCSM::Enabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_CFR", { new CCFOSlider(&CCSM::SoftnessRadius, "Graphics", "CSMSoftR", 1.0f, 4.0f, nil, (int8*)&CCSM::Enabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_SPS", { new CCFOSelect((int8*)&CSpotShadow::Enabled, "Graphics", "SpotShadow", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SELECT, "FED_SSZ", { new CCFOSelect(&CSpotShadow::MapSizeIndex, "Graphics", "SpotShadowSize", spotShadowSizes, 4, false, CSpotShadow::MapSizeAfterChange, false, (int8*)&CSpotShadow::Enabled) }, 0, 0, MENUALIGN_LEFT, \
		MENUACTION_CFO_SLIDER, "FED_SST", { new CCFOSlider(&CSpotShadow::Strength, "Graphics", "SpotShadowStrength", 0.0f, 1.0f, nil, (int8*)&CSpotShadow::Enabled) }, 0, 0, MENUALIGN_LEFT,
#else
	#define POSTFX_CSM_SELECTORS
#endif

#ifdef INVERT_LOOK_FOR_PAD
	#define INVERT_PAD_SELECTOR MENUACTION_CFO_SELECT, "FEC_ILU", { new CCFOSelect((int8*)&CPad::bInvertLook4Pad, "Controller", "InvertPad", off_on, 2, false) }, 0, 0, MENUALIGN_LEFT,
#else
	#define INVERT_PAD_SELECTOR
#endif

#ifdef GAMEPAD_MENU
	#define SELECT_CONTROLLER_TYPE  MENUACTION_CFO_SELECT, "FEC_TYP", { new CCFOSelect((int8*)&FrontEndMenuManager.m_PrefsControllerType, "Controller", "Type", controllerTypes, ARRAY_SIZE(controllerTypes), false, ControllerTypeAfterChange) }, 0, 0, MENUALIGN_LEFT,
#else
	#define SELECT_CONTROLLER_TYPE
#endif

const char *filterNames[] = { "FEM_NON", "FEM_SIM", "FEM_NRM", "FEM_MOB" };
const char *off_on[] = { "FEM_OFF", "FEM_ON" };
#ifdef MULTI_ENVMAP
// Self-describing labels — the raw "256" alone made it unclear what the
// number measured (pixel size vs mip count vs ID); the tier prefix makes
// scanning easier and lines up with the rest of the menu's quality dial.
const char *envMapSizes[] = { "Low (256)", "Medium (512)", "High (1024)", "Ultra (2048)" };
#endif
#ifdef POSTFX_HDR
const char *ssaoAlgoNames[] = { "SSAO", "GTAO", "HBAO", "Mixed" };
const char *ssaoMixNames[] = { "Min", "Average", "Multiply" };
const char *aaModeNames[] = { "Off", "FXAA", "TAA", "TAA+FXAA" };
// Quality-tier resolution selectors used in Stage 4 (resolution scalability)
// for CSM map size, spot shadow map size, IBL cube sizes, water reflection
// size, and the postfx half/quarter resolution group. Each entry stays
// readable on its own so the column header doesn't need to repeat units.
const char *cascadeMapSizes[] = { "Medium (1024)", "High (2048)", "Ultra (4096)" };
const char *spotShadowSizes[] = { "Low (256)", "Medium (512)", "High (1024)", "Ultra (2048)" };
const char *bufferResolutions[] = { "Quarter", "Half", "Full" };
const char *iblCubeCaptureSizes[] = { "Small (64)", "Standard (128)", "Large (256)", "Huge (512)" };
const char *iblIrradianceSizes[] = { "Small (16)", "Standard (32)", "Large (64)", "Huge (128)" };
const char *waterReflectionSizes[] = { "Low (256)", "Medium (512)", "High (1024)", "Ultra (2048)" };
const char *volFogStepCounts[] = { "Low (8)", "Medium (12)", "High (16)", "Very High (24)", "Ultra (32)" };
const char *volSpotMaxCounts[] = { "4 lights", "8 lights" };
#endif
#ifdef POSTFX_CSM
// Renamed: "Hard (4-tap)" was confusing (sounds like difficulty), and the
// option list now exposes the new 32-tap kernel from Stage 3.2 plus the
// VSM variant from Stage 18. Use the rendering term ("Sharp/Soft/Ultra/
// VSM") instead of a tap count alone so the user sees a quality tier,
// not an implementation detail. VSM = Variance Shadow Maps — naturally
// smooth edges via Chebyshev inequality at the cost of "light bleed"
// on thin occluders.
// Renamed: "Hard (4-tap)" was confusing (sounds like difficulty), and the
// option list now exposes the new 32-tap kernel from Stage 3.2 plus the
// VSM variant from Stage 18 and the MSM variant from Stage 29. EVSM
// (Stage 28) and Hybrid (Stage 30) will land alongside the F32 CSM
// allocation; their slots are pre-named here so the menu is forward-
// compatible (selecting them now just falls through to the highest-
// available mode until the F32 path lands).
// Use the rendering term (Sharp/Soft/Ultra/VSM/EVSM/MSM/Hybrid) instead
// of a tap count alone so the user sees a quality tier, not an
// implementation detail.
const char *csmSoftNames[] = { "Sharp (4-tap)", "Soft (16-tap)", "Ultra (32-tap)", "VSM", "EVSM", "MSM", "Hybrid" };
#endif

void RestoreDefGraphics(int8 action) {
	if (action != FEOPTION_ACTION_SELECT)
		return;

	#ifdef PS2_ALPHA_TEST
		gPS2alphaTest = false;
	#endif
	#ifdef MULTISAMPLING
		FrontEndMenuManager.m_nPrefsMSAALevel = FrontEndMenuManager.m_nDisplayMSAALevel = 0;
	#endif
	#ifdef NO_ISLAND_LOADING
	    	if (!FrontEndMenuManager.m_bGameNotLoaded) {
	    		FrontEndMenuManager.m_PrefsIslandLoading = FrontEndMenuManager.ISLAND_LOADING_LOW;
				CStreaming::RemoveUnusedBigBuildings(CGame::currLevel);
				CStreaming::RemoveUnusedBuildings(CGame::currLevel);
				CStreaming::RequestIslands(CGame::currLevel);
		        CStreaming::LoadAllRequestedModels(true);
	    	} else
	    		FrontEndMenuManager.m_PrefsIslandLoading = FrontEndMenuManager.ISLAND_LOADING_LOW;
	#endif

	// Reset the new graphics extensions back to their conservative shipping
	// defaults. CCFOSelect/Slider persists each value in settings.ini under
	// the "Graphics" category, so without this block users carry over old
	// per-effect tweaks (e.g. ACES + Gamma on a sRGB LDR scene) that hide
	// the new neutral defaults from the C++ side.
	#ifdef POSTFX_BLOOM
		CPostFX::BloomEnable = true;
		CPostFX::BloomThreshold = 0.95f;
		CPostFX::BloomKnee = 0.15f;
		CPostFX::BloomIntensity = 0.28f;
		CPostFX::BloomSaturation = 1.0f;
	#endif
	#ifdef POSTFX_TONEMAP
		// Match the C++ side defaults. ACES on, gamma off (mutually-
		// exclusive — ACES already does the sRGB rolloff); Exposure 1.2
		// to brighten the HDR midtones without over-amplifying particle
		// residue (the 1.6 attempt from the initial Stage 1 push produced
		// visible noise on dense scenes).
		CPostFX::TonemapACES = true;
		CPostFX::TonemapGamma = false;
		CPostFX::Exposure = 1.2f;
		CPostFX::Saturation = 1.05f;
		CPostFX::VignetteIntensity = 0.0f;
		CPostFX::VignetteSoftness = 0.45f;
		CPostFX::VignetteRoundness = 1.0f;
		CPostFX::CAStrength = 0.0f;
		CPostFX::CADistanceScale = 1.0f;
	#endif
	#ifdef POSTFX_FXAA
		CPostFX::FxaaEnable = true;
		CPostFX::FxaaStrength = 0.75f;
	#endif
	#ifdef POSTFX_GODRAYS
		CPostFX::GodRaysEnable = false;
		CPostFX::GodRaysExposure = 0.45f;
		CPostFX::GodRaysDensity = 0.95f;
		CPostFX::GodRaysDecay = 0.965f;
	#endif
	#ifdef SOFT_SHADOWS
		shadowPCFRadius = 1.6f;
	#endif
	#ifdef MULTI_ENVMAP
		CustomPipes::EnvMapSizeIndex = 2; // 1024
		CustomPipes::EnvMapSizePref = 1024;
	#endif
	#ifdef POSTFX_HDR
		CGBuffer::HdrEnabled = true;
		CGBuffer::GbufEnabled = true;
		CPostFX::SsaoEnable = true;
		CPostFX::SsaoRadius = 0.55f;
		CPostFX::SsaoBias = 0.03f;
		CPostFX::SsaoIntensity = 1.2f;
		CPostFX::SsaoStrength = 0.6f;
		CPostFX::SsaoAlgorithm = 0;
		CPostFX::SsaoMixMode = 1;
		CPostFX::SsaoContactStrength = 0.35f;
		CPostFX::SsaoContactRadius = 3.5f;
		CPostFX::SsaoContactMaxDz = 0.6f;
		CPostFX::ContactShadowStrength = 0.5f;
		CPostFX::ContactShadowSteps = 10;
		CPostFX::ContactShadowThickness = 1.2f;
		CPostFX::ContactShadowBias = 0.05f;
		CPostFX::IblEnabled = false;
		CPostFX::IblIntensity = 0.55f;
		CPostFX::IblHorizonExp = 2.2f;
		CPostFX::IblGroundTint = 0.6f;
		CIBL::Enabled = false;
		CIBL::ReflStrength = 1.0f;
		// Match the C++ initialisers — Standard (128) / Large (64).
		CIBL::CaptureSize = 128;
		CIBL::IrradianceSize = 64;
		CIBL::CaptureSizeIndex = 1;
		CIBL::IrradianceSizeIndex = 2;
		// Don't call Reopen here — CIBL might not be Open yet (the menu
		// can be entered before world init). The next Open call will pick
		// up the restored sizes.
		CDynamicLights::Enabled = true;
		CDynamicLights::Intensity = 1.0f;
		CDynamicLights::Reach = 1.0f;
		CDynamicLights::MaxLights = CDynamicLights::MAX_LIGHTS;
		CPostFX::SsrEnable = false;
		CPostFX::SsrMaxDistance = 30.0f;
		CPostFX::SsrStepCount = 28;
		CPostFX::SsrThickness = 0.5f;
		CPostFX::SsrStrength = 0.6f;
		CPostFX::SsrFresnelBias = 0.04f;
		CPostFX::SsrSkyFallback = 0.6f;
		// SSGI defaults — match the postfx.cpp initialisers. Off by
		// default; recommended to pair with TAA when enabled (4-direction
		// sparse sample budget produces visible per-pixel noise otherwise).
		CPostFX::SsgiEnable = false;
		CPostFX::SsgiStrength = 0.7f;
		CPostFX::SsgiMaxDistance = 18.0f;
		CPostFX::SsgiStepCount = 6;
		CPostFX::SsgiNdotLGate = 0.05f;
		// Stage 32 — bent-normal ambient bias. Default ON, subtle weight.
		// No visible effect when SSAO/GTAO disabled (gated by hdrSsao.x
		// in the shader); when GTAO is active the .gba channel of pSsaoA
		// is a real bent normal and the delta term lights up.
		CPostFX::BentNormalAmbientEnable = true;
		CPostFX::BentNormalAmbientStrength = 0.5f;
		CPostFX::BentNormalAmbientBias = 0.6f;
		// Stage 36 — SH light probes. Default OFF until the receiver
		// branch is verified in real scenes; receiver shader gates on
		// shCompose.x so the off state is a cheap [branch] skip.
		CPostFX::ShProbeEnable = false;
		CPostFX::ShProbeStrength = 0.8f;
		// Stages 37 + 38 — Occlusion + Bent N probes. Default OFF;
		// gentle defaults when enabled so toggling them in doesn't
		// dramatically shift the scene tone.
		CPostFX::OcclusionProbeEnable = false;
		CPostFX::OcclusionProbeStrength = 0.6f;
		CPostFX::BentNormalProbeBias = 0.3f;
		// Stage 39a — Atmosphere probes. Default OFF.
		CPostFX::AtmosphereProbeEnable = false;
		CPostFX::AtmosphereProbeStrength = 0.5f;
		// Stage 35 — Reflection probe tint. Default OFF.
		CPostFX::ReflectionProbeEnable = false;
		CPostFX::ReflectionProbeStrength = 0.6f;
		// Stage 15 — Deferred decals. Default OFF; Stage 15.2 will wire
		// the visual splat pass — for now the API + menu entry are
		// placeholder so game code can start spawning decals.
		CDecals::Enabled = false;
		CDecals::MaxAgeSeconds = 30.0f;
		CPostFX::WetSurfacesEnable = true;
		CPostFX::WetSurfacesIntensity = 1.0f;
		CPostFX::WetSurfacesDiffuse = 0.45f;
		CPostFX::WetSurfacesSpec = 3.2f;
		CPostFX::WetSurfacesPower = 2.5f;
		// Puddles default ON — cheap [branch] gate, fades with rain.
		CPostFX::PuddlesEnable = true;
		// Caustics default ON — only fires below water level.
		CPostFX::CausticsEnable = true;
		CPostFX::CausticsStrength = 1.0f;
		// Shoreline foam default ON — only fires near the waterline.
		CPostFX::FoamEnable = true;
		CPostFX::FoamStrength = 1.0f;
		CPostFX::DofEnable = false;
		CPostFX::DofFocusDistance = 15.0f;
		CPostFX::DofFocusRange = 6.0f;
		CPostFX::DofAperture = 0.012f;
		CPostFX::TaaEnable = false;
		CPostFX::TaaBlend = 0.12f;
		CPostFX::TaaClamp = 1.0f;
		// Motion-vector blur default OFF — opt-in via menu.
		CPostFX::MotionVecBlurEnable = false;
		CPostFX::MotionVecBlurStrength = 0.5f;
		CPostFX::MotionVecBlurMaxRadius = 0.05f;
		CPostFX::AaMode = 1;
		CPostFX::VolFogEnable = false;
		CPostFX::VolFogStrength = 0.8f;
		CPostFX::VolFogDensity = 0.015f;
		CPostFX::VolFogHeightFalloff = 0.018f;
		CPostFX::VolFogGroundZ = -10.0f;
		CPostFX::VolFogMaxDist = 350.0f;
		CPostFX::VolFogHG = 0.55f;
		CPostFX::VolFogSunBoost = 1.0f;
		// Decoupled from VolFog so the cones default to on without
		// forcing full-screen height fog. Matches the C++ initialiser
		// in postfx.cpp.
		CPostFX::VolSpotEnable = true;
		// Default VolFog raymarch quality — High (16 steps).
		CPostFX::VolFogSteps = 16;
		CPostFX::VolFogStepsIndex = 2;
	#endif
	#ifdef POSTFX_WATER_REFLECTION
		CWaterReflection::Enabled = false;	// off until water shader hooks land
		// Match the bumped C++ default: High (1024). Was 512 before
		// Stage 4 raised the floor.
		CWaterReflection::Resolution = 1024;
		CWaterReflection::ResolutionIndex = 2;
	#endif
	#ifdef POSTFX_CSM
		CCSM::Enabled = false;			// off until receiver lands in default_pp_PS
		CCSM::Strength = 0.85f;
		CCSM::Bias = 0.003f;
		CCSM::SoftnessMode = 0;
		CCSM::SoftnessRadius = 1.5f;
		// MapSize stays at the C++ default (2048). Index aligns with
		// the menu's cascadeMapSizes ordering: 0=1024, 1=2048, 2=4096.
		CCSM::MapSize = 2048;
		CCSM::MapSizeIndex = 1;
		// SpotShadow defaults — High (1024) matches the C++ initialiser
		// (was Medium 512 before Stage 4 bumped the floor).
		CSpotShadow::Enabled = false;
		CSpotShadow::Strength = 0.85f;
		CSpotShadow::MapSize = 1024;
		CSpotShadow::MapSizeIndex = 2;
	#endif

	#ifdef GRAPHICS_MENU_OPTIONS // otherwise Frontend will handle those
		FrontEndMenuManager.m_PrefsFrameLimiter = true;
		FrontEndMenuManager.m_PrefsVsyncDisp = true;
		#ifdef LEGACY_MENU_OPTIONS
			FrontEndMenuManager.m_PrefsVsync = true;
		#endif
		FrontEndMenuManager.m_PrefsUseWideScreen = false;
		FrontEndMenuManager.m_nDisplayVideoMode = FrontEndMenuManager.m_nPrefsVideoMode;
		CMBlur::BlurOn = false;
		FrontEndMenuManager.SaveSettings();
	#endif
}

void RestoreDefDisplay(int8 action) {
	if (action != FEOPTION_ACTION_SELECT)
		return;

	#ifdef CUTSCENE_BORDERS_SWITCH
		FrontEndMenuManager.m_PrefsCutsceneBorders = true;
	#endif
	#ifdef FREE_CAM
		TheCamera.bFreeCam = false;
	#endif
	#ifdef PED_CAR_DENSITY_SLIDERS
		CIniFile::LoadIniFile();
	#endif
	#ifdef GRAPHICS_MENU_OPTIONS // otherwise Frontend will handle those
		FrontEndMenuManager.m_PrefsBrightness = 256;
		FrontEndMenuManager.m_PrefsLOD = 1.2f;
		CRenderer::ms_lodDistScale = 1.2f;
		FrontEndMenuManager.m_PrefsShowSubtitles = false;
		FrontEndMenuManager.m_PrefsShowLegends = true;
		FrontEndMenuManager.m_PrefsRadarMode = 0;
		FrontEndMenuManager.m_PrefsShowHud = true;
		FrontEndMenuManager.SaveSettings();
	#endif
}

#ifdef NO_ISLAND_LOADING
const char *islandLoadingOpts[] = { "FEM_LOW", "FEM_MED", "FEM_HIG" };
void IslandLoadingAfterChange(int8 before, int8 after) {
	if (!FrontEndMenuManager.m_bGameNotLoaded) {
		if (after > FrontEndMenuManager.ISLAND_LOADING_LOW) {
		    FrontEndMenuManager.m_PrefsIslandLoading = before; // calls below needs previous mode :shrug:
		    
		    if (after == FrontEndMenuManager.ISLAND_LOADING_HIGH) {
			    CStreaming::RemoveIslandsNotUsed(LEVEL_BEACH);
			    CStreaming::RemoveIslandsNotUsed(LEVEL_MAINLAND);
			}
		    if (before == FrontEndMenuManager.ISLAND_LOADING_LOW) {
			    FrontEndMenuManager.m_PrefsIslandLoading = after;
			    CStreaming::RequestBigBuildings(CGame::currLevel);
			    
		    } else if (before == FrontEndMenuManager.ISLAND_LOADING_HIGH) {
			    FrontEndMenuManager.m_PrefsIslandLoading = after;
			    CStreaming::RequestIslands(CGame::currLevel);
		    } else
		    	    FrontEndMenuManager.m_PrefsIslandLoading = after;
		    	    
		} else { // low
		    CStreaming::RemoveUnusedBigBuildings(CGame::currLevel);
		    CStreaming::RemoveUnusedBuildings(CGame::currLevel);
		    CStreaming::RequestIslands(CGame::currLevel);
		}

		CStreaming::LoadAllRequestedModels(true);
	}

	FrontEndMenuManager.SetHelperText(0);
}
#endif

#ifdef PED_CAR_DENSITY_SLIDERS
void PedDensityChange(float before, float after) {
	CPopulation::MaxNumberOfPedsInUse = DEFAULT_MAX_NUMBER_OF_PEDS * after;
	CPopulation::MaxNumberOfPedsInUseInterior = DEFAULT_MAX_NUMBER_OF_PEDS_INTERIOR * after;
}

void CarDensityChange(float before, float after) {
	CCarCtrl::MaxNumberOfCarsInUse = DEFAULT_MAX_NUMBER_OF_CARS * after;
}
#endif

#ifndef MULTISAMPLING
void GraphicsGoBack() {
}
#else
void GraphicsGoBack() {
	FrontEndMenuManager.m_nDisplayMSAALevel = FrontEndMenuManager.m_nPrefsMSAALevel;
}

void MultiSamplingButtonPress(int8 action) {
	if (action == FEOPTION_ACTION_SELECT) {
		if (FrontEndMenuManager.m_nDisplayMSAALevel != FrontEndMenuManager.m_nPrefsMSAALevel) {
			FrontEndMenuManager.m_nPrefsMSAALevel = FrontEndMenuManager.m_nDisplayMSAALevel;
			_psSelectScreenVM(FrontEndMenuManager.m_nPrefsVideoMode);
			DMAudio.ChangeMusicMode(MUSICMODE_FRONTEND);
			DMAudio.Service();
			FrontEndMenuManager.SetHelperText(0);
			FrontEndMenuManager.SaveSettings();
		}
	} else if (action == FEOPTION_ACTION_LEFT || action == FEOPTION_ACTION_RIGHT) {
		if (FrontEndMenuManager.m_bGameNotLoaded) {
			FrontEndMenuManager.m_nDisplayMSAALevel += (action == FEOPTION_ACTION_RIGHT ? 1 : -1);

			int i = 0;
			int maxAA = RwD3D8EngineGetMaxMultiSamplingLevels();
			while (maxAA != 1) {
				i++;
				maxAA >>= 1;
			}

			if (FrontEndMenuManager.m_nDisplayMSAALevel < 0)
				FrontEndMenuManager.m_nDisplayMSAALevel = i;
			else if (FrontEndMenuManager.m_nDisplayMSAALevel > i)
				FrontEndMenuManager.m_nDisplayMSAALevel = 0;
		}
	} else if (action == FEOPTION_ACTION_FOCUSLOSS) {
		if (FrontEndMenuManager.m_nDisplayMSAALevel != FrontEndMenuManager.m_nPrefsMSAALevel) {
			FrontEndMenuManager.m_nDisplayMSAALevel = FrontEndMenuManager.m_nPrefsMSAALevel;
			FrontEndMenuManager.SetHelperText(3);
		}
	}
}

wchar* MultiSamplingDraw(bool *disabled, bool userHovering) {
	static wchar unicodeTemp[64];
	if (userHovering) {
		if (FrontEndMenuManager.m_nDisplayMSAALevel == FrontEndMenuManager.m_nPrefsMSAALevel) {
			if (FrontEndMenuManager.m_nHelperTextMsgId == 1) // Press enter to apply
				FrontEndMenuManager.ResetHelperText();
		} else {
			FrontEndMenuManager.SetHelperText(1);
		}
	} else {
		if (FrontEndMenuManager.m_nDisplayMSAALevel != FrontEndMenuManager.m_nPrefsMSAALevel) {
			FrontEndMenuManager.m_nDisplayMSAALevel = FrontEndMenuManager.m_nPrefsMSAALevel;
		}
	}

	if (!FrontEndMenuManager.m_bGameNotLoaded)
		*disabled = true;

	switch (FrontEndMenuManager.m_nDisplayMSAALevel) {
		case 0:
			return TheText.Get("FEM_OFF");
		default:
			sprintf(gString, "%iX", 1 << (FrontEndMenuManager.m_nDisplayMSAALevel));
			AsciiToUnicode(gString, unicodeTemp);
			return unicodeTemp;
	}
}
#endif

#ifdef IMPROVED_VIDEOMODE
const char* screenModes[] = { "FED_FLS", "FED_WND" };
void ScreenModeAfterChange(int8 before, int8 after)
{
	_psSelectScreenVM(FrontEndMenuManager.m_nPrefsVideoMode); // apply same resolution
	DMAudio.ChangeMusicMode(MUSICMODE_FRONTEND);
	DMAudio.Service();
	FrontEndMenuManager.SetHelperText(0);
}

#endif

#ifdef DETECT_JOYSTICK_MENU
wchar selectedJoystickUnicode[128];
int cachedButtonNum = -1;

wchar* DetectJoystickDraw(bool* disabled, bool userHovering) {

#if defined RW_GL3 && !defined LIBRW_SDL2
	int numButtons;
	int found = -1;
	const char *joyname;
	if (userHovering) {
		for (int i = 0; i <= GLFW_JOYSTICK_LAST; i++) {
			if ((joyname = glfwGetJoystickName(i))) {
				const uint8* buttons = glfwGetJoystickButtons(i, &numButtons);
				for (int j = 0; j < numButtons; j++) {
					if (buttons[j]) {
						found = i;
						break;
					}
				}
				if (found != -1)
					break;
			}
		}

		if (found != -1 && PSGLOBAL(joy1id) != found) {
			if (PSGLOBAL(joy1id) != -1 && PSGLOBAL(joy1id) != found)
				PSGLOBAL(joy2id) = PSGLOBAL(joy1id);
			else
				PSGLOBAL(joy2id) = -1;

			strcpy(gSelectedJoystickName, joyname);
			PSGLOBAL(joy1id) = found;
			cachedButtonNum = numButtons;
		}
	}
	if (PSGLOBAL(joy1id) == -1)
#elif defined XINPUT
	int found = -1;
	XINPUT_STATE xstate;
	memset(&xstate, 0, sizeof(XINPUT_STATE));
	if (userHovering) {
		for (int i = 0; i <= 3; i++) {
			if (XInputGetState(i, &xstate) == ERROR_SUCCESS) {
				if (xstate.Gamepad.bLeftTrigger || xstate.Gamepad.bRightTrigger) {
					found = i;
					break;
				}
				for (int j = XINPUT_GAMEPAD_DPAD_UP; j != XINPUT_GAMEPAD_Y << 1; j = (j << 1)) {
					if (xstate.Gamepad.wButtons & j) {
						found = i;
						break;
					}
				}
				if (found != -1)
					break;
			}
		}
		if (found != -1 && CPad::XInputJoy1 != found) {
			// We should never leave pads -1, so we can process them when they're connected and kinda support hotplug.
			CPad::XInputJoy2 = (CPad::XInputJoy1 == -1 ? (found + 1) % 4 : CPad::XInputJoy1);
			CPad::XInputJoy1 = found;
			cachedButtonNum = 0; // fake too, because xinput bypass CControllerConfig
		}
	}
	sprintf(gSelectedJoystickName, "%d", CPad::XInputJoy1); // fake, on xinput we only store gamepad ids(thanks MS) so this is a temp variable to be used below
	if (CPad::XInputJoy1 == -1)
#endif
		AsciiToUnicode("Not found", selectedJoystickUnicode);
	else
		AsciiToUnicode(gSelectedJoystickName, selectedJoystickUnicode);

	return selectedJoystickUnicode;
}

void DetectJoystickGoBack() {
	if (cachedButtonNum != -1) {
#ifdef LOAD_INI_SETTINGS
		ControlsManager.InitDefaultControlConfigJoyPad(cachedButtonNum);
		SaveINIControllerSettings();
#else
		// Otherwise no way to save gSelectedJoystickName or ms_padButtonsInited anyway :shrug: Why do you even use this config.??
#endif
		cachedButtonNum = -1;
	}
}
#endif

#ifdef GAMEPAD_MENU
const char* controllerTypes[] = { "FEC_DS2", "FEC_DS3", "FEC_DS4", "FEC_360", "FEC_ONE", "FEC_NSW" };
void ControllerTypeAfterChange(int8 before, int8 after)
{
	FrontEndMenuManager.LoadController(after);
}
#endif

CMenuScreenCustom aScreens[] = {
	// MENUPAGE_STATS = 0
	{ "FEH_STA", MENUPAGE_NONE, nil, nil,
		MENUACTION_GOBACK, "FEDS_TB", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 190, 320, MENUALIGN_RIGHT,
	},

	// MENUPAGE_NEW_GAME = 1
	{ "FEP_STG", MENUPAGE_NONE, nil, nil,
		MENUACTION_CHANGEMENU, "FES_NGA", {nil, SAVESLOT_NONE, MENUPAGE_NEW_GAME_RELOAD}, 320, 155, MENUALIGN_CENTER,
		MENUACTION_CHANGEMENU, "FES_LOA",  {nil, SAVESLOT_NONE, MENUPAGE_CHOOSE_LOAD_SLOT}, 0, 0, MENUALIGN_CENTER,
		MENUACTION_CHANGEMENU, "FES_DEL", {nil, SAVESLOT_NONE, MENUPAGE_CHOOSE_DELETE_SLOT}, 0, 0, MENUALIGN_CENTER,
		MENUACTION_GOBACK, "FEDS_TB", {nil, SAVESLOT_NONE, 0}, 0, 0, MENUALIGN_CENTER,
	},

	// MENUPAGE_BRIEFS = 2
	{ "FEH_BRI", MENUPAGE_NONE, nil, nil,
		MENUACTION_GOBACK, "FEDS_TB", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 190, 320, MENUALIGN_RIGHT,
	},

	// MENUPAGE_SOUND_SETTINGS = 3
	{ "FEH_AUD", MENUPAGE_OPTIONS, nil, nil,
		MENUACTION_MUSICVOLUME,		"FEA_MUS", {nil, SAVESLOT_NONE, MENUPAGE_SOUND_SETTINGS}, 40, 76, MENUALIGN_LEFT,
		MENUACTION_SFXVOLUME,		"FEA_SFX", {nil, SAVESLOT_NONE, MENUPAGE_SOUND_SETTINGS}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_MP3VOLUMEBOOST,	"FEA_MPB", {nil, SAVESLOT_NONE, MENUPAGE_SOUND_SETTINGS}, 0, 0, MENUALIGN_LEFT,
#ifdef EXTERNAL_3D_SOUND
		MENUACTION_AUDIOHW,			"FEA_3DH", {nil, SAVESLOT_NONE, MENUPAGE_SOUND_SETTINGS}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_SPEAKERCONF,		"FEA_SPK", {nil, SAVESLOT_NONE, MENUPAGE_SOUND_SETTINGS}, 0, 0, MENUALIGN_LEFT,
#endif
		MENUACTION_DYNAMICACOUSTIC,	"FET_DAM", {nil, SAVESLOT_NONE, MENUPAGE_SOUND_SETTINGS}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_RADIO,			"FEA_RSS", {nil, SAVESLOT_NONE, MENUPAGE_SOUND_SETTINGS}, 0, 0, MENUALIGN_LEFT,
#ifdef EXTERNAL_3D_SOUND
		MENUACTION_RESTOREDEF,		"FET_DEF", {nil, SAVESLOT_NONE, MENUPAGE_SOUND_SETTINGS}, 320, 367, MENUALIGN_CENTER,
#else
		MENUACTION_RESTOREDEF,		"FET_DEF", {nil, SAVESLOT_NONE, MENUPAGE_SOUND_SETTINGS}, 320, 327, MENUALIGN_CENTER,
#endif
		MENUACTION_GOBACK,			"FEDS_TB", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 0, 0, MENUALIGN_CENTER,
	},

	// MENUPAGE_DISPLAY_SETTINGS = 4
#ifndef GRAPHICS_MENU_OPTIONS
	{ "FEH_DIS", MENUPAGE_OPTIONS, new CCustomScreenLayout({40, 78, 25, true}), nil,
		MENUACTION_BRIGHTNESS,	"FED_BRI", {nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_DRAWDIST,	"FEM_LOD", {nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS}, 0, 0, MENUALIGN_LEFT,
		DENSITY_SLIDERS
#ifdef LEGACY_MENU_OPTIONS
		MENUACTION_FRAMESYNC,	"FEM_VSC", {nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS}, 0, 0, MENUALIGN_LEFT,
#endif
		MENUACTION_FRAMELIMIT,	"FEM_FRM", {nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS}, 0, 0, MENUALIGN_LEFT,
#if defined LEGACY_MENU_OPTIONS && !defined EXTENDED_COLOURFILTER
		MENUACTION_TRAILS,		"FED_TRA", {nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS}, 0, 0, MENUALIGN_LEFT,
#endif
		MENUACTION_SUBTITLES,	"FED_SUB", {nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_WIDESCREEN,	"FED_WIS", {nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_LEGENDS,		"MAP_LEG", {nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_RADARMODE,	"FED_RDR", {nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_HUD,			"FED_HUD", {nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_SCREENRES,	"FED_RES", {nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS}, 0, 0, MENUALIGN_LEFT,
		VIDEOMODE_SELECTOR
		MULTISAMPLING_SELECTOR
		ISLAND_LOADING_SELECTOR
		DUALPASS_SELECTOR
		CUTSCENE_BORDERS_TOGGLE
		FREE_CAM_TOGGLE
		POSTFX_SELECTORS
		// re3.cpp inserts here pipeline selectors if neo/neo.txd exists and EXTENDED_PIPELINES defined
		MENUACTION_RESTOREDEF,	"FET_DEF", {nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS}, 320, 0, MENUALIGN_CENTER,
		MENUACTION_GOBACK,		"FEDS_TB", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 320, 0, MENUALIGN_CENTER,
	},
#else
	{ "FEH_DIS", MENUPAGE_OPTIONS, new CCustomScreenLayout({40, 78, 25, true}), nil,
		MENUACTION_BRIGHTNESS,	"FED_BRI", { nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS }, 0, 0, MENUALIGN_LEFT,
		MENUACTION_DRAWDIST,	"FEM_LOD", { nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS }, 0, 0, MENUALIGN_LEFT,
		DENSITY_SLIDERS
		CUTSCENE_BORDERS_TOGGLE
		FREE_CAM_TOGGLE
		MENUACTION_LEGENDS,		"MAP_LEG", { nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS }, 0, 0, MENUALIGN_LEFT,
		MENUACTION_RADARMODE,	"FED_RDR", { nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS }, 0, 0, MENUALIGN_LEFT,
		MENUACTION_HUD,			"FED_HUD", { nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS }, 0, 0, MENUALIGN_LEFT,
		MENUACTION_SUBTITLES,	"FED_SUB", { nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS }, 0, 0, MENUALIGN_LEFT,
		MENUACTION_CFO_DYNAMIC,	"FET_DEF", { new CCFODynamic(nil, nil, nil, nil, RestoreDefDisplay) }, 320, 0, MENUALIGN_CENTER,
		MENUACTION_GOBACK,		"FEDS_TB", { nil, SAVESLOT_NONE, MENUPAGE_NONE}, 320, 0, MENUALIGN_CENTER,
	},
#endif

	// MENUPAGE_LANGUAGE_SETTINGS = 5
	{ "FEH_LAN", MENUPAGE_OPTIONS, nil, nil,
		MENUACTION_LANG_ENG,	"FEL_ENG", {nil, SAVESLOT_NONE, MENUPAGE_LANGUAGE_SETTINGS}, 320, 132, MENUALIGN_CENTER,
		MENUACTION_LANG_FRE,	"FEL_FRE", {nil, SAVESLOT_NONE, MENUPAGE_LANGUAGE_SETTINGS}, 0, 0, MENUALIGN_CENTER,
		MENUACTION_LANG_GER,	"FEL_GER", {nil, SAVESLOT_NONE, MENUPAGE_LANGUAGE_SETTINGS}, 0, 0, MENUALIGN_CENTER,
		MENUACTION_LANG_ITA,	"FEL_ITA", {nil, SAVESLOT_NONE, MENUPAGE_LANGUAGE_SETTINGS}, 0, 0, MENUALIGN_CENTER,
		MENUACTION_LANG_SPA,    "FEL_SPA", {nil, SAVESLOT_NONE, MENUPAGE_LANGUAGE_SETTINGS}, 0, 0, MENUALIGN_CENTER,
		MENUACTION_GOBACK,		"FEDS_TB", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 0, 0, MENUALIGN_CENTER,
	},

	// MENUPAGE_MAP = 6
	{ "FEH_MAP", MENUPAGE_NONE, nil, nil,
		 MENUACTION_GOBACK,	"FEDS_TB", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 70, 380, MENUALIGN_CENTER,
	},

	// MENUPAGE_NEW_GAME_RELOAD = 7
	{ "FES_NGA", MENUPAGE_NEW_GAME, nil, nil,
		MENUACTION_LABEL,		"FESZ_QR",	{nil, SAVESLOT_NONE,	0}, 0, 0, 0,
		MENUACTION_NO,			"FEM_NO",	{nil, SAVESLOT_NONE,	MENUPAGE_NEW_GAME}, 320, 200, MENUALIGN_CENTER,
		MENUACTION_NEWGAME,		"FEM_YES",	{nil, SAVESLOT_NONE,	MENUPAGE_NEW_GAME_RELOAD}, 320, 225, MENUALIGN_CENTER,
	},

	// MENUPAGE_CHOOSE_LOAD_SLOT = 8
	{ "FET_LG", MENUPAGE_NEW_GAME, nil, nil,
		MENUACTION_CHECKSAVE,	"FEM_SL1", {nil, SAVESLOT_1,		0}, 40, 90, MENUALIGN_LEFT,
		MENUACTION_CHECKSAVE,	"FEM_SL2", {nil, SAVESLOT_2,		0}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_CHECKSAVE,	"FEM_SL3", {nil, SAVESLOT_3,		0}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_CHECKSAVE,	"FEM_SL4", {nil, SAVESLOT_4,		0}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_CHECKSAVE,	"FEM_SL5", {nil, SAVESLOT_5,		0}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_CHECKSAVE,	"FEM_SL6", {nil, SAVESLOT_6,		0}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_CHECKSAVE,	"FEM_SL7", {nil, SAVESLOT_7,		0}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_CHECKSAVE,	"FEM_SL8", {nil, SAVESLOT_8,		0}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_GOBACK,		"FEDS_TB", {nil, SAVESLOT_NONE,	0}, 320, 345, MENUALIGN_CENTER,
	},

	// MENUPAGE_CHOOSE_DELETE_SLOT = 9
	{ "FES_DEL", MENUPAGE_NEW_GAME, nil, nil,
		MENUACTION_CHECKSAVE,	"FEM_SL1",	{nil, SAVESLOT_1,		0}, 40, 90, MENUALIGN_LEFT,
		MENUACTION_CHECKSAVE,	"FEM_SL2",	{nil, SAVESLOT_2,		0}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_CHECKSAVE,	"FEM_SL3",	{nil, SAVESLOT_3,		0}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_CHECKSAVE,	"FEM_SL4",	{nil, SAVESLOT_4,		0}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_CHECKSAVE,	"FEM_SL5",	{nil, SAVESLOT_5,		0}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_CHECKSAVE,	"FEM_SL6",	{nil, SAVESLOT_6,		0}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_CHECKSAVE,	"FEM_SL7",	{nil, SAVESLOT_7,		0}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_CHECKSAVE,	"FEM_SL8",	{nil, SAVESLOT_8,		0}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_GOBACK,		"FEDS_TB",	{nil, SAVESLOT_NONE,	0}, 320, 345, MENUALIGN_CENTER,
	},

	// MENUPAGE_LOAD_SLOT_CONFIRM = 10
	{ "FET_LG", MENUPAGE_CHOOSE_LOAD_SLOT, nil, nil,
		 MENUACTION_LABEL,		"FESZ_QL",	{nil, SAVESLOT_NONE,	0}, 0, 0, 0,
		 MENUACTION_NO,			"FEM_NO",	{nil, SAVESLOT_NONE,	MENUPAGE_CHOOSE_LOAD_SLOT}, 320, 200, MENUALIGN_CENTER,
		 MENUACTION_YES,		"FEM_YES",	{nil, SAVESLOT_NONE,	MENUPAGE_LOADING_IN_PROGRESS}, 320, 225, MENUALIGN_CENTER,
	},

	// MENUPAGE_DELETE_SLOT_CONFIRM = 11
	{ "FES_DEL", MENUPAGE_CHOOSE_DELETE_SLOT, nil, nil,
		 MENUACTION_LABEL,		"FESZ_QD",	{nil, SAVESLOT_NONE,  MENUPAGE_NONE}, 0, 0, 0,
		 MENUACTION_NO,			"FEM_NO",	{nil, SAVESLOT_NONE,  MENUPAGE_CHOOSE_DELETE_SLOT}, 320, 200, MENUALIGN_CENTER,
		 MENUACTION_YES,		"FEM_YES",	{nil, SAVESLOT_NONE,	MENUPAGE_DELETING_IN_PROGRESS}, 320, 225, MENUALIGN_CENTER,
	},

	// MENUPAGE_LOADING_IN_PROGRESS = 12
	{ "FET_LG", MENUPAGE_CHOOSE_LOAD_SLOT, nil, nil,
	},

	// MENUPAGE_DELETING_IN_PROGRESS = 13
	{ "FES_DEL", MENUPAGE_CHOOSE_DELETE_SLOT, nil, nil,
	},

	// MENUPAGE_DELETE_SUCCESSFUL = 14
	{ "FES_DEL", MENUPAGE_NEW_GAME, nil, nil,
		 MENUACTION_LABEL,		"FES_DSC",	{nil, SAVESLOT_NONE,	0}, 0, 0, 0,
		 MENUACTION_CHANGEMENU,	"FEM_OK",	{nil, SAVESLOT_NONE,	MENUPAGE_NEW_GAME}, 320, 225, MENUALIGN_CENTER,
	},

	// MENUPAGE_CHOOSE_SAVE_SLOT = 15
	{ "FET_SG", MENUPAGE_DISABLED, nil, nil,
		MENUACTION_SAVEGAME,			"FEM_SL1", {nil, SAVESLOT_1,		MENUPAGE_SAVE_OVERWRITE_CONFIRM}, 40, 90, MENUALIGN_LEFT,
		MENUACTION_SAVEGAME,			"FEM_SL2", {nil, SAVESLOT_2,		MENUPAGE_SAVE_OVERWRITE_CONFIRM}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_SAVEGAME,			"FEM_SL3", {nil, SAVESLOT_3,		MENUPAGE_SAVE_OVERWRITE_CONFIRM}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_SAVEGAME,			"FEM_SL4", {nil, SAVESLOT_4,		MENUPAGE_SAVE_OVERWRITE_CONFIRM}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_SAVEGAME,			"FEM_SL5", {nil, SAVESLOT_5,		MENUPAGE_SAVE_OVERWRITE_CONFIRM}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_SAVEGAME,			"FEM_SL6", {nil, SAVESLOT_6,		MENUPAGE_SAVE_OVERWRITE_CONFIRM}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_SAVEGAME,			"FEM_SL7", {nil, SAVESLOT_7,		MENUPAGE_SAVE_OVERWRITE_CONFIRM}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_SAVEGAME,			"FEM_SL8", {nil, SAVESLOT_8,		MENUPAGE_SAVE_OVERWRITE_CONFIRM}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_RESUME_FROM_SAVEZONE,"FESZ_CA", {nil, SAVESLOT_NONE,	0}, 320, 345, MENUALIGN_CENTER,
	},

	// MENUPAGE_SAVE_OVERWRITE_CONFIRM = 16
	{ "FET_SG", MENUPAGE_CHOOSE_SAVE_SLOT, nil, nil,
		MENUACTION_LABEL,		"FESZ_QZ", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 0, 0, 0,
		MENUACTION_NO,			"FEM_NO", {nil, SAVESLOT_NONE, MENUPAGE_CHOOSE_SAVE_SLOT}, 320, 200, MENUALIGN_CENTER,
		MENUACTION_YES,			"FEM_YES",  {nil, SAVESLOT_NONE, MENUPAGE_SAVING_IN_PROGRESS}, 320, 225, MENUALIGN_CENTER,
	},

	// MENUPAGE_SAVING_IN_PROGRESS = 17
	{ "FET_SG", MENUPAGE_CHOOSE_SAVE_SLOT, nil, nil,
	},

	// MENUPAGE_SAVE_SUCCESSFUL = 18
	{ "FET_SG", MENUPAGE_CHOOSE_SAVE_SLOT, nil, nil,
		MENUACTION_LABEL,					"FES_SSC",	{nil, SAVESLOT_LABEL,	MENUPAGE_NONE}, 0, 0, 0,
		MENUACTION_RESUME_FROM_SAVEZONE,	"FEM_OK",	{nil, SAVESLOT_NONE,	MENUPAGE_CHOOSE_SAVE_SLOT}, 320, 225, MENUALIGN_CENTER,
	},

	// MENUPAGE_SAVE_CUSTOM_WARNING = 19
	{ "FET_SG", MENUPAGE_NONE, nil, nil,
		MENUACTION_LABEL,		"",			{nil, SAVESLOT_NONE, 0}, 0, 0, 0,
		MENUACTION_CHANGEMENU,	"FEM_OK",	{nil, SAVESLOT_NONE, MENUPAGE_CHOOSE_SAVE_SLOT}, 320, 225, MENUALIGN_CENTER,
	},

	// MENUPAGE_SAVE_CHEAT_WARNING = 20
	{ "FET_SG", MENUPAGE_NEW_GAME, nil, nil,
		MENUACTION_LABEL,		"FES_CHE",	{nil, SAVESLOT_NONE,	MENUPAGE_NONE}, 0, 0, 0,
		MENUACTION_CHANGEMENU,	"FEM_OK",	{nil, SAVESLOT_NONE,	MENUPAGE_CHOOSE_SAVE_SLOT}, 320, 225, MENUALIGN_CENTER,
	},

	// MENUPAGE_SKIN_SELECT = 21
	{ "FET_PS", MENUPAGE_OPTIONS, nil, nil,
		 MENUACTION_GOBACK,		"FEDS_TB",	{nil, SAVESLOT_NONE, MENUPAGE_OPTIONS}, 0, 0, 0,
	},

	// MENUPAGE_SAVE_UNUSED = 22
	{ "FET_SG", MENUPAGE_NEW_GAME, nil, nil,
		 MENUACTION_LABEL,		"FED_LWR",	{nil, SAVESLOT_NONE,	0}, 0, 0, 0,
		 MENUACTION_CHANGEMENU,	"FEC_OKK",	{nil, SAVESLOT_NONE,	MENUPAGE_CHOOSE_SAVE_SLOT}, 0, 0, 0,
	},

	// MENUPAGE_SAVE_FAILED = 23
	{ "FET_SG", MENUPAGE_CHOOSE_SAVE_SLOT, nil, nil,
		 MENUACTION_LABEL,		"FEC_SVU",	{nil, SAVESLOT_NONE,	0}, 0, 0, 0,
		 MENUACTION_CHANGEMENU,	"FEC_OKK",	{nil, SAVESLOT_NONE,	MENUPAGE_CHOOSE_SAVE_SLOT}, 0, 0, 0,
	},

	// MENUPAGE_SAVE_FAILED_2 = 24
	{ "FET_LG", MENUPAGE_CHOOSE_SAVE_SLOT, nil, nil,
		 MENUACTION_LABEL,		"FEC_SVU",	{nil, SAVESLOT_NONE,	0}, 0, 0, 0,
	},

	// MENUPAGE_LOAD_FAILED = 25
	{ "FET_LG", MENUPAGE_NEW_GAME, nil, nil,
		 MENUACTION_LABEL,		"FEC_LUN",	{nil, SAVESLOT_NONE,  0}, 0, 0, 0,
		 MENUACTION_GOBACK,		"FEDS_TB",	{nil, SAVESLOT_NONE,  MENUPAGE_NEW_GAME}, 0, 0, 0,
	},

	// MENUPAGE_CONTROLLER_PC = 26
	{ "FET_CTL", MENUPAGE_OPTIONS, new CCustomScreenLayout({0, 0, MENU_DEFAULT_LINE_HEIGHT, false, false, 150}), nil,
#ifdef PC_PLAYER_CONTROLS
		MENUACTION_CTRLMETHOD,	"FET_STI", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC}, 320, 150, MENUALIGN_CENTER,
		MENUACTION_KEYBOARDCTRLS,"FEC_RED", {nil, SAVESLOT_NONE, MENUPAGE_KEYBOARD_CONTROLS}, 0, 0, MENUALIGN_CENTER,
#else
		MENUACTION_KEYBOARDCTRLS,"FEC_RED", {nil, SAVESLOT_NONE, MENUPAGE_KEYBOARD_CONTROLS}, 320, 150, MENUALIGN_CENTER,
#endif
#ifdef GAMEPAD_MENU
		MENUACTION_CHANGEMENU,	"FET_AGS", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_SETTINGS}, 0, 0, MENUALIGN_CENTER,
#endif
#ifdef DETECT_JOYSTICK_MENU
		MENUACTION_CHANGEMENU,	"FEC_JOD", {nil, SAVESLOT_NONE, MENUPAGE_DETECT_JOYSTICK}, 0, 0, MENUALIGN_CENTER,
#endif
		MENUACTION_CHANGEMENU,	"FEC_MOU", {nil, SAVESLOT_NONE, MENUPAGE_MOUSE_CONTROLS}, 0, 0, MENUALIGN_CENTER,
		MENUACTION_RESTOREDEF,	"FET_DEF", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC}, 320, 0, MENUALIGN_CENTER,
		MENUACTION_GOBACK,		"FEDS_TB", {nil, SAVESLOT_NONE, 0}, 320, 0, MENUALIGN_CENTER,
   },

	// MENUPAGE_OPTIONS = 27
	{ "FET_OPT", MENUPAGE_NONE, nil, nil,
#ifdef GTA_HANDHELD
		 MENUACTION_CHANGEMENU,		"FEO_CON", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_SETTINGS}, 320, 132, MENUALIGN_CENTER,
#else
		 MENUACTION_CHANGEMENU,		"FEO_CON", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC}, 320, 132, MENUALIGN_CENTER,
#endif
		 MENUACTION_LOADRADIO,		"FEO_AUD", {nil, SAVESLOT_NONE, MENUPAGE_SOUND_SETTINGS}, 0, 0, MENUALIGN_CENTER,
		 MENUACTION_CHANGEMENU,		"FEO_DIS", {nil, SAVESLOT_NONE, MENUPAGE_DISPLAY_SETTINGS}, 0, 0, MENUALIGN_CENTER,
#ifdef GRAPHICS_MENU_OPTIONS
		 MENUACTION_CHANGEMENU,		"FET_GFX", {nil, SAVESLOT_NONE, MENUPAGE_GRAPHICS_SETTINGS}, 0, 0, MENUALIGN_CENTER,
#endif
		 MENUACTION_CHANGEMENU,		"FEO_LAN", {nil, SAVESLOT_NONE, MENUPAGE_LANGUAGE_SETTINGS}, 0, 0, MENUALIGN_CENTER,
		 MENUACTION_PLAYERSETUP,	"FET_PS", {nil, SAVESLOT_NONE, MENUPAGE_SKIN_SELECT}, 0, 0, MENUALIGN_CENTER,
		 MENUACTION_GOBACK,			"FEDS_TB", {nil, SAVESLOT_NONE, 0}, 0, 0, MENUALIGN_CENTER,
   },

	// MENUPAGE_EXIT = 28
	{ "FET_QG", MENUPAGE_NONE, nil, nil,
		MENUACTION_LABEL,		"FEQ_SRE",	{nil, SAVESLOT_NONE, 0}, 0, 0, 0,
		MENUACTION_DONTCANCEL,	"FEM_NO",	{nil, SAVESLOT_NONE, MENUPAGE_NONE}, 320, 200, MENUALIGN_CENTER,
		MENUACTION_CANCELGAME,	"FEM_YES",	{nil, SAVESLOT_NONE, MENUPAGE_NONE}, 320, 225, MENUALIGN_CENTER,
   },

	// MENUPAGE_START_MENU = 29
	{ "FEM_MM", MENUPAGE_DISABLED, nil, nil,
		  MENUACTION_CHANGEMENU,	"FEP_STG",	{nil, SAVESLOT_NONE,	MENUPAGE_NEW_GAME}, 320, 170, MENUALIGN_CENTER,
		  MENUACTION_CHANGEMENU,	"FEP_OPT",	{nil, SAVESLOT_NONE,	MENUPAGE_OPTIONS}, 0, 0, MENUALIGN_CENTER,
		  MENUACTION_CHANGEMENU,	"FEP_QUI",	{nil, SAVESLOT_NONE,	MENUPAGE_EXIT}, 0, 0, MENUALIGN_CENTER,
   },

	// MENUPAGE_KEYBOARD_CONTROLS = 30
	{ "FET_STI", MENUPAGE_CONTROLLER_PC, nil, nil,
   },

	// MENUPAGE_MOUSE_CONTROLS = 31
	{ "FEC_MOU", MENUPAGE_CONTROLLER_PC, nil, nil,
		MENUACTION_MOUSESENS,	"FEC_MSH",	{nil, SAVESLOT_NONE, MENUPAGE_MOUSE_CONTROLS}, 40, 170, MENUALIGN_LEFT,
		MENUACTION_INVVERT,		"FEC_IVV",	{nil, SAVESLOT_NONE, MENUPAGE_MOUSE_CONTROLS}, 0, 0, MENUALIGN_LEFT,
#ifndef GAMEPAD_MENU
	   INVERT_PAD_SELECTOR
#endif
		MENUACTION_MOUSESTEER,	"FET_MST",	{nil, SAVESLOT_NONE, MENUPAGE_MOUSE_CONTROLS}, 0, 0, MENUALIGN_LEFT,
		MENUACTION_GOBACK,		"FEDS_TB",	{nil, SAVESLOT_NONE, 0}, 320, 0, MENUALIGN_CENTER,
		//MENUACTION_GOBACK,		"FEDS_TB",	{nil, SAVESLOT_NONE, 0}, 320, 260, MENUALIGN_CENTER, // original y
   },

	// MENUPAGE_PAUSE_MENU = 32
	{ "FET_PAU", MENUPAGE_DISABLED, nil, nil,
		MENUACTION_RESUME,		"FEP_RES",	{nil, SAVESLOT_NONE, 0}, 320, 120, MENUALIGN_CENTER,
		MENUACTION_CHANGEMENU,	"FEH_SGA",	{nil, SAVESLOT_NONE, MENUPAGE_NEW_GAME}, 0, 0, MENUALIGN_CENTER,
		MENUACTION_CHANGEMENU,	"FEH_MAP",	{nil, SAVESLOT_NONE, MENUPAGE_MAP}, 0, 0, MENUALIGN_CENTER,
		MENUACTION_CHANGEMENU,	"FEP_STA",	{nil, SAVESLOT_NONE, MENUPAGE_STATS}, 0, 0, MENUALIGN_CENTER,
		MENUACTION_CHANGEMENU,	"FEH_BRI",	{nil, SAVESLOT_NONE, MENUPAGE_BRIEFS}, 0, 0, MENUALIGN_CENTER,
		MENUACTION_CHANGEMENU,	"FET_OPT",	{nil, SAVESLOT_NONE, MENUPAGE_OPTIONS}, 0, 0, MENUALIGN_CENTER,
		MENUACTION_CHANGEMENU,	"FEP_QUI",	{nil, SAVESLOT_NONE, MENUPAGE_EXIT}, 0, 0, MENUALIGN_CENTER,
   },

	// MENUPAGE_NONE = 33
	{ "", 0, nil, nil, },

#ifdef GAMEPAD_MENU
#ifdef GTA_HANDHELD
	{ "FET_AGS", MENUPAGE_OPTIONS, new CCustomScreenLayout({40, 78, 25, true, true}), nil,
#else
	{ "FET_AGS", MENUPAGE_CONTROLLER_PC, new CCustomScreenLayout({40, 78, 25, true, true}), nil,
#endif
		MENUACTION_CTRLCONFIG,		"FEC_CCF", { nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_SETTINGS }, 40, 76, MENUALIGN_LEFT,
		MENUACTION_CTRLDISPLAY,		"FEC_CDP", { nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_SETTINGS }, 0, 0, MENUALIGN_LEFT,
		INVERT_PAD_SELECTOR
		MENUACTION_CTRLVIBRATION,	"FEC_VIB", { nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_SETTINGS }, 0, 0, MENUALIGN_LEFT,
		SELECT_CONTROLLER_TYPE
		MENUACTION_GOBACK,		"FEDS_TB", { nil, SAVESLOT_NONE, MENUPAGE_NONE }, 0, 0, MENUALIGN_LEFT,
	},
#endif
#ifdef LEGACY_MENU_OPTIONS
	// MENUPAGE_DEBUG_MENU = 18
	{ "FED_DBG", MENUPAGE_NONE, nil, nil,
		MENUACTION_RELOADIDE,	"FED_RID", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 0, 0, 0,
		MENUACTION_SETDBGFLAG,	"FED_DFL", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 0, 0, 0,
		MENUACTION_SWITCHBIGWHITEDEBUGLIGHT,	"FED_DLS", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 0, 0, 0,
		MENUACTION_COLLISIONPOLYS,	"FED_SCP", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 0, 0, 0,
		MENUACTION_GOBACK,		"FEDS_TB", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 0, 0, 0,
   },

	// MENUPAGE_CONTROLLER_PC_OLD1 = 36
	{ "FET_CTL", MENUPAGE_CONTROLLER_PC, nil, nil,
		MENUACTION_GETKEY,	"FEC_PLB", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC_OLD1}, 0, 0, 0,
		MENUACTION_GETKEY,	"FEC_CWL", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC_OLD1}, 0, 0, 0,
		MENUACTION_GETKEY,	"FEC_CWR", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC_OLD1}, 0, 0, 0,
		MENUACTION_GETKEY,	"FEC_LKT", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC_OLD1}, 0, 0, 0,
		MENUACTION_GETKEY,	"FEC_PJP", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC_OLD1}, 0, 0, 0,
		MENUACTION_GETKEY,	"FEC_PSP", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC_OLD1}, 0, 0, 0,
		MENUACTION_GETKEY,	"FEC_TLF", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC_OLD1}, 0, 0, 0,
		MENUACTION_GETKEY,	"FEC_TRG", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC_OLD1}, 0, 0, 0,
		MENUACTION_GETKEY,	"FEC_CCM", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC_OLD1}, 0, 0, 0,
		MENUACTION_GOBACK,		"FEDS_TB", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 0, 0, 0,
   },

	// MENUPAGE_CONTROLLER_PC_OLD2 = 37
	{ "FET_CTL", MENUPAGE_CONTROLLER_PC, nil, nil,

	},

	// MENUPAGE_CONTROLLER_PC_OLD3 = 38
	{ "FET_CTL", MENUPAGE_CONTROLLER_PC, nil, nil,
		 MENUACTION_GETKEY,	"FEC_LUP", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC_OLD3}, 0, 0, 0,
		 MENUACTION_GETKEY,	"FEC_LDN", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC_OLD3}, 0, 0, 0,
		 MENUACTION_GETKEY,	"FEC_SMS", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC_OLD3}, 0, 0, 0,
		 MENUACTION_SHOWHEADBOB,	"FEC_GSL", {nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_PC_OLD3}, 0, 0, 0,
		 MENUACTION_GOBACK,		"FEDS_TB", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 0, 0, 0,
   },

	// MENUPAGE_CONTROLLER_PC_OLD4 = 39
	{ "FET_CTL", MENUPAGE_CONTROLLER_PC, nil, nil,

	},

	// MENUPAGE_CONTROLLER_DEBUG = 40
	{ "FEC_DBG", MENUPAGE_CONTROLLER_PC, nil, nil,
		 MENUACTION_GETKEY,	"FEC_TGD",	{nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_DEBUG}, 0, 0, 0,
		 MENUACTION_GETKEY,	"FEC_TDO",	{nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_DEBUG}, 0, 0, 0,
		 MENUACTION_GETKEY,	"FEC_TSS",	{nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_DEBUG}, 0, 0, 0,
		 MENUACTION_GETKEY,	"FEC_SMS",	{nil, SAVESLOT_NONE, MENUPAGE_CONTROLLER_DEBUG}, 0, 0, 0,
		 MENUACTION_GOBACK,	"FEDS_TB",	{nil, SAVESLOT_NONE, MENUPAGE_NONE}, 0, 0, 0,
   },
#endif

#ifdef GRAPHICS_MENU_OPTIONS
	// MENUPAGE_GRAPHICS_SETTINGS
	{ "FET_GFX", MENUPAGE_OPTIONS, new CCustomScreenLayout({40, 78, 25, true, true}), GraphicsGoBack,

#ifndef GTA_HANDHELD
		MENUACTION_SCREENRES,	"FED_RES", { nil, SAVESLOT_NONE, MENUPAGE_GRAPHICS_SETTINGS }, 0, 0, MENUALIGN_LEFT,
#endif
		MENUACTION_WIDESCREEN,	"FED_WIS", { nil, SAVESLOT_NONE, MENUPAGE_GRAPHICS_SETTINGS }, 0, 0, MENUALIGN_LEFT,
		VIDEOMODE_SELECTOR
#ifdef LEGACY_MENU_OPTIONS
		MENUACTION_FRAMESYNC,	"FEM_VSC", {nil, SAVESLOT_NONE, MENUPAGE_GRAPHICS_SETTINGS}, 0, 0, MENUALIGN_LEFT,
#endif
		MENUACTION_FRAMELIMIT,	"FEM_FRM", { nil, SAVESLOT_NONE, MENUPAGE_GRAPHICS_SETTINGS }, 0, 0, MENUALIGN_LEFT,
		MULTISAMPLING_SELECTOR
		ISLAND_LOADING_SELECTOR
		DUALPASS_SELECTOR
#ifdef EXTENDED_COLOURFILTER
		POSTFX_SELECTORS
#elif defined LEGACY_MENU_OPTIONS
		MENUACTION_TRAILS,		"FED_TRA", { nil, SAVESLOT_NONE, MENUPAGE_GRAPHICS_SETTINGS }, 0, 0, MENUALIGN_LEFT,
#endif
		POSTFX_HDR_SELECTORS
		POSTFX_WATER_REFLECTION_SELECTORS
		POSTFX_CSM_SELECTORS
		POSTFX_BLOOM_SELECTORS
		POSTFX_TONEMAP_SELECTORS
		POSTFX_FXAA_SELECTORS
		POSTFX_TAA_SELECTORS
		POSTFX_GODRAYS_SELECTORS
		POSTFX_SHADOWS_SELECTORS
		POSTFX_ENVMAP_SELECTORS
		// re3.cpp inserts here pipeline selectors if neo/neo.txd exists and EXTENDED_PIPELINES defined
		MENUACTION_CFO_DYNAMIC,	"FET_DEF", { new CCFODynamic(nil, nil, nil, nil, RestoreDefGraphics) }, 320, 0, MENUALIGN_CENTER,
		MENUACTION_GOBACK,		"FEDS_TB", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 320, 0, MENUALIGN_CENTER,
	},
#endif

#ifdef DETECT_JOYSTICK_MENU
	// MENUPAGE_DETECT_JOYSTICK
	{ "FEC_JOD", MENUPAGE_CONTROLLER_PC, new CCustomScreenLayout({0, 0, 0, false, false, 30}), DetectJoystickGoBack,
		MENUACTION_LABEL,	"FEC_JPR", { nil, SAVESLOT_NONE, MENUPAGE_NONE }, 0, 0, 0,
		MENUACTION_CFO_DYNAMIC,	"FEC_JDE", { new CCFODynamic(nil, nil, nil, DetectJoystickDraw, nil) }, 80, 200, MENUALIGN_LEFT,
		MENUACTION_GOBACK,		"FEDS_TB", {nil, SAVESLOT_NONE, MENUPAGE_NONE}, 320, 225, MENUALIGN_CENTER,
	},
#endif

		
#ifdef MISSION_REPLAY
	// MENUPAGE_MISSION_RETRY = 57 on mobile

	{ "M_FAIL", MENUPAGE_DISABLED, nil, nil,
		MENUACTION_LABEL,			"FESZ_RM",  { nil, SAVESLOT_NONE, MENUPAGE_NONE }, 0, 0, 0,
		MENUACTION_CHANGEMENU,		"FEM_YES",  { nil, SAVESLOT_NONE, MENUPAGE_LOADING_IN_PROGRESS }, 320, 200, MENUALIGN_CENTER,
		MENUACTION_REJECT_RETRY,	"FEM_NO",   { nil, SAVESLOT_NONE, MENUPAGE_NONE }, 320, 225, MENUALIGN_CENTER,
	},
#endif

	// MENUPAGE_OUTRO = 34
	{ "", 0, nil, nil, },
};

#endif
#endif
