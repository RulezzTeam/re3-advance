float4x4	combinedMat	: register(c0);
float4x4	worldMat	: register(c4);
float3x3	normalMat	: register(c8);
// c11 = viewParams: .x = 1/farClip (for normalised view-space depth), .y =
// near plane, .z = far plane, .w = reserved. Fed by uploadMatrices when
// G-buffer is active; safe to read in all VS variants because c11 is a gap
// between normalMat (c8..c10) and matCol (c12) and is not used by skin
// bones (which start at c41).
float4		viewParams	: register(c11);
float4		matCol		: register(c12);
float4		surfProps	: register(c13);
float4		fogData	: register(c14);
float4		ambientLight	: register(c15);

#define surfAmbient (surfProps.x)
#define surfSpecular (surfProps.y)
#define surfDiffuse (surfProps.z)

#define fogStart (fogData.x)
#define fogEnd (fogData.y)
#define fogRange (fogData.z)
#define fogDisable (fogData.w)

#include "lighting.h"

int numDirLights : register(i0);
int numPointLights : register(i1);
int numSpotLights : register(i2);
int4 firstLight : register(c16);
Light lights[8] : register(c17);

#define firstDirLight (firstLight.x)
#define firstPointLight (firstLight.y)
#define firstSpotLight (firstLight.z)
