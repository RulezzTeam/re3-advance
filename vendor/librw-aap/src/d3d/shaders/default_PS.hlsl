// Default pixel shader.
//
// Variants compiled from this file:
//   default_PS              - legacy LDR (no PP, no GBUFFER)
//   default_tex_PS          - legacy LDR with diffuse texture
//   default_pp_PS / _tex_PS - per-pixel lighting, LDR (single RT)
//   default_pp_gbuf_PS / _tex_PS - per-pixel lighting, HDR-friendly, writes MRT:
//                                  COLOR0 = scene (HDR linear), COLOR1 = packed
//                                  world-normal (rgb) + linear depth (a)

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
#ifdef GBUFFER
	float  ViewDepth	: TEXCOORD3;
#endif
#endif
};

sampler2D tex0 : register(s0);

float4 fogColor : register(c0);

#ifdef PER_PIXEL_LIGHTING
// .xyz = world-space camera position
// .w   = Blinn-Phong specular power (32 by default, tweaked by host)
float4 eyePosPS : register(c42);

// Procedural IBL — sky/horizon/ground gradient sampled by world-space normal.
// Host uploads these once per scene render from CTimeCycle (sky colours +
// derived horizon mix). Intensity = 0 → zero contribution, so the per-frame
// upload doesn't cost anything when IBL is off.
//
// .rgb = colour, .a unused (kept for c-register alignment).
float4 iblSky      : register(c44);	// hemisphere zenith (N.z = +1)
float4 iblHorizon  : register(c45);	// hemisphere ring (|N.z| ≈ 0)
float4 iblGround   : register(c46);	// hemisphere nadir (N.z = -1, muted)
// .x = intensity multiplier (0 = off)
// .y = horizon falloff exponent (1.0 = linear, larger = sharper horizon band)
// .z = directional-light tinting (0..1 — lerps lit toward iblSky on shadowed faces)
// .w = unused
float4 iblParams   : register(c47);
#endif

#ifdef GBUFFER
struct PS_out {
	float4 Color		: COLOR0;	// HDR linear scene
	float4 NormalDepth	: COLOR1;	// RGB = world normal * 0.5 + 0.5, A = linear depth
};
#endif


// ComputeShadedColor: shared core that computes the final fragment colour.
// The two main() entry points (single-RT vs MRT) call this and then either
// return float4 (legacy) or pack the result into PS_out with the G-buffer
// channel filled in.
float4 ComputeShadedColor(VS_out input)
{
	float4 color = input.Color;

#ifdef PER_PIXEL_LIGHTING
	float3 N = normalize(input.WorldNormal);
	float3 lit = float3(0.0, 0.0, 0.0);
	float3 spec = float3(0.0, 0.0, 0.0);

	int i;
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

	// IBL — procedural hemisphere gradient driven by world normal. iblParams.x
	// gates the whole contribution; when the host sets it to 0 we trade three
	// mul-adds for nothing. The horizon weight uses 1 - |N.z|^p so a larger
	// exponent gives a sharper sky/horizon transition (good for daytime).
	{
		float up   = saturate( N.z);
		float down = saturate(-N.z);
		float hor  = 1.0 - saturate(pow(abs(N.z), max(iblParams.y, 0.5)));
		float3 iblCol = up   * iblSky.rgb
		              + down * iblGround.rgb
		              + hor  * iblHorizon.rgb;
		// Treat IBL as a soft diffuse — modulate by the material diffuse
		// coefficient so unlit materials (like UI quads, particles) don't
		// pick up sky bleed when this branch is somehow hit.
		lit += iblCol * iblParams.x * surfDiffuse;
	}

	// In LDR mode we clamp prelight+ambient+lit BEFORE matCol — matches the
	// legacy VS path so brightness stays identical to stock GTA.
	// In HDR/G-buffer mode we only reject negatives so >1 light values
	// survive into the bloom + tonemap pipeline.
#ifdef GBUFFER
	float3 baseLight = max(color.rgb + lit, 0.0);
#else
	float3 baseLight = saturate(color.rgb + lit);
#endif
	color.rgb = baseLight * matCol.rgb + spec;
	color.a *= matCol.a;
#endif

#ifdef TEX
	color *= tex2D(tex0, input.TexCoord0.xy);
#endif

	// Fog: linear lerp. A smoothstep variant was tried once and pushed mid-
	// distance pixels toward fogColor — looked blue in Vice City.
	color.rgb = lerp(fogColor.rgb, color.rgb, input.TexCoord0.z);
	return color;
}


#ifdef GBUFFER
PS_out main(VS_out input)
{
	PS_out o;
	o.Color = ComputeShadedColor(input);

	// World-space normal packed into [0,1] for RGBA16F storage. Re-normalise
	// before storage so the consumer (SSAO, CSM) gets unit-length normals
	// even on degenerate mesh edges.
	float3 N = normalize(input.WorldNormal);
	o.NormalDepth = float4(N * 0.5 + 0.5, saturate(input.ViewDepth));
	return o;
}
#else
float4 main(VS_out input) : COLOR
{
	return ComputeShadedColor(input);
}
#endif
