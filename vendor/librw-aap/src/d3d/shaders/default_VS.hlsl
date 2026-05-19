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
	float3 TexCoord0	: TEXCOORD0;	// also fog
	float4 Color		: COLOR0;
#ifdef PER_PIXEL_LIGHTING
	float3 WorldNormal	: TEXCOORD1;
	float3 WorldPos		: TEXCOORD2;
#ifdef GBUFFER
	// Linear view-space depth normalised by 1/farClip, ready to drop into
	// the G-buffer alpha channel (matches CGBuffer::ResolveLinearDepth).
	float  ViewDepth	: TEXCOORD3;
#endif
#endif
};


VS_out main(in VS_in input)
{
	VS_out output;

	output.Position = mul(combinedMat, input.Position);
	float3 Vertex = mul(worldMat, input.Position).xyz;
	float3 Normal = mul(normalMat, input.Normal);

	output.TexCoord0.xy = input.TexCoord;

#ifdef PER_PIXEL_LIGHTING
	// Hand world-space data + un-modulated prelight+ambient to the pixel
	// shader. The PS is responsible for the final clamp(prelight+ambient+lit)
	// and the matCol multiply — that order mirrors the legacy VS path so
	// the resulting brightness matches stock GTA.
	output.WorldNormal = Normal;
	output.WorldPos = Vertex;
	output.Color = input.Prelight;
	output.Color.rgb += ambientLight.rgb * surfAmbient;
#ifdef GBUFFER
	// View-space depth normalised so PS can write a linear depth value
	// straight into the G-buffer alpha channel without an inverse-projection
	// reconstruction at the consumer end. .w of the clip-space position is
	// already the linear view-space Z (positive away from camera).
	output.ViewDepth = saturate(output.Position.w * viewParams.x);
#endif
#else
	output.Color = input.Prelight;
	output.Color.rgb += ambientLight.rgb * surfAmbient;

	int i;
#ifdef DIRECTIONALS
	for(i = 0; i < numDirLights; i++)
		output.Color.xyz += DoDirLight(lights[i+firstDirLight], Normal)*surfDiffuse;
#endif
#ifdef POINTLIGHTS
	for(i = 0; i < numPointLights; i++)
		output.Color.xyz += DoPointLight(lights[i+firstPointLight], Vertex.xyz, Normal)*surfDiffuse;
#endif
#ifdef SPOTLIGHTS
	for(i = 0; i < numSpotLights; i++)
		output.Color.xyz += DoSpotLight(lights[i+firstSpotLight], Vertex.xyz, Normal)*surfDiffuse;
#endif
	// PS2 clamps before material color
	output.Color = clamp(output.Color, 0.0, 1.0);
	output.Color *= matCol;
#endif

	output.TexCoord0.z = clamp((output.Position.w - fogEnd)*fogRange, fogDisable, 1.0);

	return output;
}
