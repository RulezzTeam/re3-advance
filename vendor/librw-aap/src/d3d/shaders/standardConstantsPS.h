// Pixel-shader binding of the lighting constants used by the per-pixel
// lighting variant. Mirrors standardConstants.h but with float-register
// counts (i0..i2 are vertex-shader-only).
//
// The host C++ code (rw::d3d::uploadLightsPS) is responsible for filling
// these registers from the same WorldLights struct that feeds the VS
// uploads — see d3drender.cpp.

float4		matCol		: register(c12);
float4		surfProps	: register(c13);
float4		ambientLight	: register(c15);

#define surfAmbient (surfProps.x)
#define surfSpecular (surfProps.y)
#define surfDiffuse (surfProps.z)

#include "lighting.h"

float4 numLightsPS : register(c41);
int4   firstLight  : register(c16);
Light  lights[8]   : register(c17);

#define numDirLights   ((int)numLightsPS.x)
#define numPointLights ((int)numLightsPS.y)
#define numSpotLights  ((int)numLightsPS.z)

#define firstDirLight   (firstLight.x)
#define firstPointLight (firstLight.y)
#define firstSpotLight  (firstLight.z)
