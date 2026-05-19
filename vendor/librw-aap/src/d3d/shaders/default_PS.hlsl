// Default pixel shader.
//
// PER_PIXEL_LIGHTING variant:
//   - Lambertian diffuse + point/spot attenuation in PS so large polygons
//     and point lights look correct regardless of tessellation.
//   - Blinn-Phong specular highlight gated by surfSpecular > 0.
//   - Fog applied per-pixel with smoothstep for a softer falloff than the
//     legacy linear lerp.
//
// Non-PP variant: same as the original LDR pass — vertex colour modulated
// by the texture, plus the linear fog lerp.

#ifdef PER_PIXEL_LIGHTING
#include "standardConstantsPS.h"
#endif

struct VS_out {
	float4 Position		: POSITION;
	float3 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
#ifdef PER_PIXEL_LIGHTING
	float3 WorldNormal	: TEXCOORD1;
	float3 WorldPos		: TEXCOORD2;
#endif
};

sampler2D tex0 : register(s0);

float4 fogColor : register(c0);

#ifdef PER_PIXEL_LIGHTING
// .xyz = world-space camera position
// .w   = Blinn-Phong specular power (32 by default, tweaked by host)
float4 eyePosPS : register(c42);
#endif

float4 main(VS_out input) : COLOR
{
	float4 color = input.Color;

#ifdef PER_PIXEL_LIGHTING
	float3 N = normalize(input.WorldNormal);
	float3 lit = float3(0.0, 0.0, 0.0);
	float3 spec = float3(0.0, 0.0, 0.0);

	int i;
	// ps_3_0 supports dynamic flow control via [loop]; no unroll needed.
#ifdef DIRECTIONALS
	[loop]
	for(i = 0; i < numDirLights; i++)
		lit += DoDirLight(lights[i+firstDirLight], N) * surfDiffuse;
#endif
#ifdef POINTLIGHTS
	[loop]
	for(i = 0; i < numPointLights; i++)
		lit += DoPointLight(lights[i+firstPointLight], input.WorldPos, N) * surfDiffuse;
#endif
#ifdef SPOTLIGHTS
	[loop]
	for(i = 0; i < numSpotLights; i++)
		lit += DoSpotLight(lights[i+firstSpotLight], input.WorldPos, N) * surfDiffuse;
#endif

	// Specular only when the material actually has a non-zero spec value.
	// Cheap branch saves ~20 ALU on the vast majority of fragments.
	[branch]
	if(surfSpecular > 0.001){
		float3 V = normalize(eyePosPS.xyz - input.WorldPos);
		float power = max(eyePosPS.w, 8.0);
#ifdef DIRECTIONALS
		[loop]
		for(i = 0; i < numDirLights; i++)
			spec += DoDirLightSpec(lights[i+firstDirLight], N, V, power) * surfSpecular;
#endif
	}

	color.rgb += lit * matCol.rgb + spec;
	color.rgb = saturate(color.rgb);
#endif

#ifdef TEX
	color *= tex2D(tex0, input.TexCoord0.xy);
#endif
	// Smoothstep fog gives a softer near-field falloff than a raw lerp; in
	// the absence of fog the host passes a fog factor of 0..1 already clamped.
	float fogFactor = smoothstep(0.0, 1.0, input.TexCoord0.z);
	color.rgb = lerp(fogColor.rgb, color.rgb, fogFactor);
	return color;
}
