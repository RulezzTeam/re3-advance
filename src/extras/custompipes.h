#pragma once

#ifdef LIBRW
#ifdef EXTENDED_PIPELINES

namespace CustomPipes {


extern rw::TexDictionary *neoTxd;

struct CustomMatExt
{
	rw::Texture *glossTex;
	bool haveGloss;
};
extern rw::int32 CustomMatOffset;
inline CustomMatExt *GetCustomMatExt(rw::Material *mat) {
	return PLUGINOFFSET(CustomMatExt, mat, CustomMatOffset);
}


struct Color
{
	float r, g, b, a;
	Color(void) {}
	Color(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {}
};

class InterpolatedValue
{
public:
	virtual void Read(char *s, int line, int field) = 0;
};

class InterpolatedFloat : public InterpolatedValue
{
public:
	float data[24][NUMWEATHERS];
	float curInterpolator;
	float curVal;

	InterpolatedFloat(float init);
	void Read(char *s, int line, int field);
	float Get(void);
};

class InterpolatedColor : public InterpolatedValue
{
public:
	Color data[24][NUMWEATHERS];
	float curInterpolator;
	Color curVal;

	InterpolatedColor(const Color &init);
	void Read(char *s, int line, int field);
	Color Get(void);
};

class InterpolatedLight : public InterpolatedColor
{
public:
	InterpolatedLight(const Color &init) : InterpolatedColor(init) {}
	void Read(char *s, int line, int field);
};

char *ReadTweakValueTable(char *fp, InterpolatedValue &interp);





void CustomPipeRegister(void);
void CustomPipeRegisterGL(void);
void CustomPipeInit(void);
void CustomPipeShutdown(void);
void SetTxdFindCallback(void);

extern bool bRenderingEnvMap;
extern int32 EnvMapSize;
extern rw::Camera *EnvMapCam;
extern rw::Texture *EnvMapTex;
#ifdef MULTI_ENVMAP
// 16 cubemap-style sample slots covering player, mid-range traffic, and
// distant skyline. At 1024x1024 this is ~128MB VRAM (color + depth), which
// is comfortable on any GPU with 2+ GB.
#define NUM_ENVMAPS 16
extern rw::Camera *EnvMapCams[NUM_ENVMAPS];
extern rw::Texture *EnvMapTexs[NUM_ENVMAPS];
// Returns the slot best matched to a world-space vehicle position.
int EnvMapSlotFor(const rw::V3d &worldPos);
// Live-tweakable resolution selector (menu-bound). The value is read at
// CustomPipeInit() time so changes apply on next launch.
extern int32 EnvMapSizePref;
// Menu-friendly index: 0=256, 1=512, 2=1024, 3=2048 (saved as int8).
extern int8 EnvMapSizeIndex;
// Rebuilds every EnvMap camera/texture with the current EnvMapSizePref.
// Safe to call after Scene.world is initialised.
void RebuildEnvMaps(void);
// Menu AfterChange callback — translates index to size and rebuilds.
void EnvMapSizeAfterChange(int8 before, int8 after);
#endif
extern rw::Texture *EnvMaskTex;
void EnvMapRender(void);

enum {
	VEHICLEPIPE_MATFX,
	VEHICLEPIPE_NEO
};
extern int32 VehiclePipeSwitch;
extern float VehicleShininess;
extern float VehicleSpecularity;
extern InterpolatedFloat Fresnel;
extern InterpolatedFloat Power;
extern InterpolatedLight DiffColor;
extern InterpolatedLight SpecColor;
extern rw::ObjPipeline *vehiclePipe;
void CreateVehiclePipe(void);
void DestroyVehiclePipe(void);
void AttachVehiclePipe(rw::Atomic *atomic);
void AttachVehiclePipe(rw::Clump *clump);

extern bool LightmapEnable;
extern float LightmapMult;
extern InterpolatedFloat WorldLightmapBlend;
extern rw::ObjPipeline *worldPipe;
void CreateWorldPipe(void);
void DestroyWorldPipe(void);
void AttachWorldPipe(rw::Atomic *atomic);
void AttachWorldPipe(rw::Clump *clump);

extern bool GlossEnable;
extern float GlossMult;
extern rw::ObjPipeline *glossPipe;
void CreateGlossPipe(void);
void DestroyGlossPipe(void);
void AttachGlossPipe(rw::Atomic *atomic);
void AttachGlossPipe(rw::Clump *clump);
rw::Texture *GetGlossTex(rw::Material *mat);

extern bool RimlightEnable;
extern float RimlightMult;
extern InterpolatedColor RampStart;
extern InterpolatedColor RampEnd;
extern InterpolatedFloat Offset;
extern InterpolatedFloat Scale;
extern InterpolatedFloat Scaling;
extern rw::ObjPipeline *rimPipe;
extern rw::ObjPipeline *rimSkinPipe;
void CreateRimLightPipes(void);
void DestroyRimLightPipes(void);
void AttachRimPipe(rw::Atomic *atomic);
void AttachRimPipe(rw::Clump *clump);

}

#endif

namespace WorldRender{
extern int numBlendInsts[3];
void AtomicFirstPass(RpAtomic *atomic, int pass);
void AtomicFullyTransparent(RpAtomic *atomic, int pass, int fadeAlpha);
void RenderBlendPass(int pass);
}

#endif
