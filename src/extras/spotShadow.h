#pragma once

#ifdef POSTFX_HDR

// Spot Shadow Map — one shared 512² depth raster + a perspective light
// camera positioned at the brightest active point/spot light each
// frame. The receiver in default_pp_PS samples this map via s9 to
// occlude that single light's contribution.
//
// Why one map? Vice City's typical scene has 1-4 dominant point lights
// (player car headlights, the nearest street lamp). Picking the
// brightest by luminance × inverse-distance² gives the highest visual
// impact for the cost of a single shadow pass. Future work could
// extend this to N maps walking the top-N lights.
//
// VRAM: 1 × 512² R32F = 1 MB. Trivial.
// Cost: one extra scene depth pass with frustum culling against the
// light's cone — typically a small percentage of the visible atomics.

class CSpotShadow
{
public:
	static RwRaster *depthRT;	// R32F (falls back to F16 RGBA if needed)
	static RwRaster *zBuffer;	// shared
	static RwCamera *lightCam;	// perspective FOV ~60..120°

	static bool Enabled;
	static int32 MapSize;		// 256 / 512 / 1024 / 2048
	static int8  MapSizeIndex;	// 0..3 = 256/512/1024/2048 — menu binding
	static float Strength;		// 0..1 mix into the lit term
	static float Bias;		// shadow comparison bias
	static float Softness;		// pixel-space PCF radius

	// Currently-active light snapshot, picked each frame from the
	// point/spot light list. World position + range/intensity. Range
	// drives the light camera's far plane; intensity drives picking.
	struct ActiveLight {
		float worldPos[3];
		float colour[3];
		float radius;
		float intensity;
		bool valid;
	};
	static ActiveLight current;
	static float lightViewProj[16];	// world → cascade clip, row-major

	static void InitOnce(void);
	static void Open(RwCamera *cam);
	static void Close(void);
	static void PickActiveLight(RwCamera *cam);
	static void RenderShadowMap(RwCamera *cam);
	static void BindReceiver(void);
	static void UnbindReceiver(void);

	// Menu CCFOSelect AfterChange — maps MapSizeIndex to MapSize and
	// reallocates the depth raster + Z buffer at the new resolution.
	static void MapSizeAfterChange(int8 before, int8 after);
};

#endif
