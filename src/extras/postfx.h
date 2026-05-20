#pragma once

#ifdef EXTENDED_COLOURFILTER

class CPostFX
{
public:
	enum {
		POSTFX_OFF,
		POSTFX_SIMPLE,
		POSTFX_NORMAL,
		POSTFX_MOBILE
	};
	static RwRaster *pFrontBuffer;
	static RwRaster *pBackBuffer;
	static bool bJustInitialised;
	static int EffectSwitch;
	static bool BlurOn;	// or use CMblur for that?
	static bool MotionBlurOn;	// or use CMblur for that?
	static float Intensity;

	// smooth blur color
	enum { NUMAVERAGE = 20 };
	static int PrevRed[NUMAVERAGE], AvgRed;
	static int PrevGreen[NUMAVERAGE], AvgGreen;
	static int PrevBlue[NUMAVERAGE], AvgBlue;
	static int PrevAlpha[NUMAVERAGE], AvgAlpha;
	static int Next;
	static int NumValues;

#ifdef POSTFX_BLOOM
	static RwRaster *pBloomA;
	static RwRaster *pBloomB;
	static bool BloomEnable;
	static float BloomThreshold;	// luminance above which bloom kicks in (0..1)
	static float BloomKnee;		// soft knee around threshold
	static float BloomIntensity;	// strength of the additive composite
	static float BloomSaturation;	// bloom-only saturation boost (1.0 = neutral)
#endif
#ifdef POSTFX_FXAA
	static bool FxaaEnable;
	static float FxaaStrength;	// 0..1 lerp toward FXAA result
#endif
#ifdef POSTFX_HDR
	// TAA (also gated by POSTFX_HDR so we have a clean colour pipeline +
	// matching RT sizes). FXAA is auto-disabled while TaaEnable is on
	// because both passes do edge smoothing — running together adds
	// blur without quality gain.
	static RwRaster *pTaaHistA;
	static RwRaster *pTaaHistB;
	static bool TaaEnable;
	static float TaaBlend;		// 0.05..0.20 typical; smaller = more temporal accumulation
	static float TaaClamp;		// neighbourhood AABB expansion (1.0 default)
	static int   TaaFrameIdx;	// 0 or 1 ping-pong slot for the current history
#endif
#ifdef POSTFX_GODRAYS
	static bool GodRaysEnable;
	static float GodRaysDensity;	// sample spacing (~1.0 default)
	static float GodRaysDecay;	// per-sample falloff (~0.94 default)
	static float GodRaysExposure;	// brightness scale (~0.7)
	static float GodRaysWeight;	// per-sample weight inside loop (kept simple)
#endif
#ifdef POSTFX_TONEMAP
	static bool TonemapACES;
	static bool TonemapGamma;
	static float Exposure;		// linear exposure multiplier (1.0 = neutral)
	static float Saturation;	// 1.0 = neutral, 0 = grayscale, >1 = vivid
	static float VignetteIntensity; // 0..1 (0 = off)
	static float VignetteSoftness;  // 0..1
	static float VignetteRoundness; // ~1.0
	static float CAStrength;	// chromatic aberration radial offset (0 = off, ~0.005 typical)
	static float CADistanceScale;	// 0 = uniform, 1 = scaled by radius (default 1)
#endif
#ifdef POSTFX_HDR
	// SSAO (consumes CGBuffer::pGbufNormalDepth). Output blended in
	// hdrResolve_PS via sampler s1 = pSsaoA.
	static RwRaster *pSsaoA;
	static RwRaster *pSsaoB;
	static bool SsaoEnable;
	static float SsaoRadius;	// world units (~0.5..2.0)
	static float SsaoBias;		// (~0.01..0.05)
	static float SsaoIntensity;	// occlusion scale (~1..3)
	static float SsaoStrength;	// final compose lerp (0 = off, 1 = full effect)
	static float SsaoPower;		// AO curve power (>1 = darker, <1 = softer)

	// Contact AO — short cross-tap ray-march on top of the hemisphere
	// kernel. Catches sub-pixel contacts the main kernel jumps over.
	static float SsaoContactStrength;	// 0 = off
	static float SsaoContactRadius;	// screen-space pixels (2..6 typical)
	static float SsaoContactMaxDz;	// metres — discard farther occluders

	// Screen-Space Reflections — half-res world-space march that samples
	// pHdrScene on hit. Composed into hdrResolve_PS via Fresnel weight.
	static RwRaster *pSsrA;		// half-res RGBA8 reflection RT
	static bool SsrEnable;
	static float SsrMaxDistance;	// world units (10..80 typical)
	static int SsrStepCount;	// 8..32
	static float SsrThickness;	// depth-window for accepting hits
	static float SsrStrength;	// compose lerp in hdrResolve_PS
	static float SsrFresnelBias;	// Schlick F0 (0.04 = dielectric default)
	static void RenderSSR(RwCamera *cam);

	// Image-Based Lighting — procedural hemisphere gradient (sky + horizon
	// + ground) sampled by world normal inside default_pp_PS as a soft
	// ambient term. Colours are derived from CTimeCycle's SkyTop/SkyBottom
	// each frame, so dusk/dawn naturally bleed warm ambient onto upward-
	// facing surfaces.
	static bool IblEnabled;
	static float IblIntensity;	// 0 = off, 1 = neutral, 2 = vivid
	static float IblHorizonExp;	// horizon falloff exponent (1..6)
	static float IblExposure;	// CTimeCycle → linear scale before upload
	static float IblGroundTint;	// 0 = neutral grey, 1 = warm earthy ground
	static void UpdateIBL(void);	// host-side: pulls colours, uploads to librw

	// Volumetric fog — single-scattering height fog with Henyey-Greenstein
	// directional in-scatter from the sun. Composed inside hdrResolve_PS
	// using the G-buffer normal/depth raster on sampler s2.
	static bool VolFogEnable;
	static float VolFogStrength;	// 0..1, lerps the fogged colour onto the original
	static float VolFogDensity;	// extinction per metre at ground level (~0.01..0.05)
	static float VolFogHeightFalloff;// 1/m; fog thins exp(-h*falloff) above ground
	static float VolFogGroundZ;	// world Z that counts as ground (~ -5..5 in VC)
	static float VolFogMaxDist;	// march distance for sky pixels (metres)
	static float VolFogHG;		// Henyey-Greenstein g (0=iso, 0.7=sun halo)
	static float VolFogSunBoost;	// multiplier on sun colour (HDR-aware)
#endif

	static void InitOnce(void);
	static void Open(RwCamera *cam);
	static void Close(void);
	static void RenderOverlayBlur(RwCamera *cam, int32 r, int32 g, int32 b, int32 a);
	static void RenderOverlaySniper(RwCamera *cam, int32 r, int32 g, int32 b, int32 a);
	static void RenderOverlayShader(RwCamera *cam, int32 r, int32 g, int32 b, int32 a);
	static void RenderMotionBlur(RwCamera *cam, uint32 blur);
#ifdef POSTFX_BLOOM
	static void RenderBloom(RwCamera *cam);
#endif
#ifdef POSTFX_FXAA
	static void RenderFXAA(RwCamera *cam);
#endif
#ifdef POSTFX_GODRAYS
	static void RenderGodRays(RwCamera *cam);
#endif
	static void Render(RwCamera *cam, uint32 red, uint32 green, uint32 blue, uint32 blur, int32 type, uint32 bluralpha);
#ifdef POSTFX_HDR
	// HDR -> LDR tonemap resolve. Reads CGBuffer::pHdrScene and writes the
	// tonemapped result into the camera's backbuffer. Called once per frame
	// right after CGBuffer::EndScenePass, before any LDR pass (motion blur,
	// screen droplets, UI).
	static void ResolveHDR(RwCamera *cam);
	// Screen-space AO consuming the G-buffer. Renders to pSsaoA which is
	// then sampled by hdrResolve_PS. Call between EndScenePass and
	// ResolveHDR.
	static void RenderSSAO(RwCamera *cam);
	// Temporal AA — reads pBackBuffer (current) + pTaaHist[idx] (history),
	// writes the blended result back to the backbuffer + the next history
	// slot. Call once per frame, after ResolveHDR, instead of FXAA.
	static void RenderTAA(RwCamera *cam);
#endif
	static void SmoothColor(uint32 red, uint32 green, uint32 blue, uint32 alpha);
	static bool NeedBackBuffer(void);
	static bool NeedFrontBuffer(int32 type);
	static void GetBackBuffer(RwCamera *cam);
	static bool UseBlurColours(void) { return EffectSwitch != POSTFX_SIMPLE; }
};

#ifdef SOFT_SHADOWS
extern void *shadowPCF_PS;
extern float shadowPCFTexelSize[4];
extern float shadowPCFRadius;	// kernel radius in texels (1.0..3.0 typical)
void EnableShadowPCF(int textureSize);
void DisableShadowPCF(void);
#endif

#endif
