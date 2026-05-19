// Neo vehicle vertex shader.
//
// Hands the world-space normal, position and (pre-vertex) diffuse colour
// to the pixel shader. All Fresnel, reflection and specular maths now lives
// in neoVehicle_PS.hlsl so the highlights stay sharp on dense quads
// (windshields, sloped panels) instead of being smeared per-vertex.

#include "standardConstants.h"

struct VS_in
{
	float4 Position		: POSITION;
	float3 Normal		: NORMAL;
	float2 TexCoord		: TEXCOORD0;
	float4 Prelight		: COLOR0;
};

struct VS_out {
	float4 Position		: POSITION;
	float3 TexCoord0	: TEXCOORD0;	// .xy uv, .z fog
	float4 Color		: COLOR0;
	float3 WorldNormal	: TEXCOORD1;
	float3 WorldPos		: TEXCOORD2;
};

float3 eye        : register(c41);
float4 reflProps  : register(c42);
#define lightStrength (reflProps.y)

VS_out main(in VS_in input)
{
	VS_out output;

	output.Position = mul(combinedMat, input.Position);
	float3 V = mul(worldMat, input.Position).xyz;
	float3 N = mul(normalMat, input.Normal);

	output.TexCoord0.xy = input.TexCoord;
	output.WorldNormal = N;
	output.WorldPos = V;

	// Cheap per-vertex Lambertian — keeps non-specular surfaces correctly lit
	// before the PS overlays reflections and highlights.
	output.Color = input.Prelight;
	output.Color.rgb += ambientLight.rgb * surfAmbient * lightStrength;

	int i;
	for(i = 0; i < numDirLights; i++)
		output.Color.xyz += DoDirLight(lights[i+firstDirLight], N) * surfDiffuse * lightStrength;
	output.Color = clamp(output.Color, 0.0, 1.0);
	output.Color *= matCol;

	output.TexCoord0.z = clamp((output.Position.w - fogEnd)*fogRange, fogDisable, 1.0);

	return output;
}
