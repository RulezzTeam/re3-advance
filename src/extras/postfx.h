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
