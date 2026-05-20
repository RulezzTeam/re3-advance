// Neo vehicle pixel shader — per-pixel Fresnel, reflection, and specular.
//
// The pixel shader runs on ps_3_0 with dynamic branching enabled, so the
// 5 specular lights are evaluated only when the material actually has
// surfProps.specular > 0.

struct VS_out {
	float4 Position		: POSITION;
	float3 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
	float3 WorldNormal	: TEXCOORD1;
	float3 WorldPos		: TEXCOORD2;
};

sampler2D tex0 : register(s0);	// vehicle albedo
sampler2D tex1 : register(s1);	// planar environment map (slot picked by host)
samplerCUBE iblCaptureCube : register(s2);	// real-time sky+sun reflection cube (CIBL::captureCube)

float4 fogColor   : register(c0);
float3 eyePosPS   : register(c41);
float4 reflProps  : register(c42);
// .x = Fresnel base term (0..1; 0 = pure Schlick)
// .y = light strength multiplier
// .z = shininess (mix to reflection)
// .w = specularity (specular highlight scale)

// specLights array runs c43..c57 (5 entries × 3 vec4); c58+ is free.
// .x = cube reflection blend (0 = legacy planar only, 1 = cube only)
// .y = unused
// .z = unused
// .w = unused
float4 reflProps2 : register(c58);

struct SpecLight {
	float4 color;	// .a = power
	float4 pos;	// (unused but kept to match VS upload layout)
	float4 dir;	// .w = power slot
};
SpecLight specLights[5] : register(c43);

// Dynamic point lights — same data block as default_pp_PS so the host
// uploads once and every pp/world/vehicle pipe samples the same set.
// c100 = active light count, c101..c164 = 32 lights × (pos+radius,
// colour+intensity). This is what gives vehicle paint a warm bath
// from another car's headlights / from street lamps / from gunfire,
// where previously cars only got the planar env reflection + Blinn-
// Phong directional from the sun.
float4 dynLightCount : register(c100);
float4 dynLightData[64] : register(c101);

float3 ReflectV(float3 V, float3 N)
{
	return N * dot(V, N) * 2.0 - V;
}

// Dynamic point-light receiver for the vehicle pipe. Same inverse-square
// + radius-cutoff × NdotL math as the pp_PS variant in
// `vendor/librw-aap/src/d3d/shaders/default_PS.hlsl::ApplyDynamicPointLights`
// but WITHOUT the slot-0 spot shadow integration — vehicle bodies don't
// run through pp_PS (they have their own pipe) and the spot shadow map
// is only authored for the static-world pass, so applying it here would
// introduce a self-shadow seam between car and street lamp. Keep this
// copy in sync with default_PS for the attenuation curve so cars and
// the road they sit on get visually consistent illumination.
float3 ApplyDynamicPointLights(float3 worldPos, float3 N)
{
	float3 sum = float3(0, 0, 0);
	int n = (int)dynLightCount.x;
	[loop]
	for(int i = 0; i < n; i++){
		float4 lp = dynLightData[i*2 + 0];
		float4 lc = dynLightData[i*2 + 1];
		float3 toL = lp.xyz - worldPos;
		float distSq = dot(toL, toL);
		float radius = lp.w;
		if(distSq > radius * radius)
			continue;
		float dist = sqrt(max(distSq, 1e-6));
		float3 L = toL / dist;
		float ndotl = saturate(dot(N, L));
		float t = 1.0 - dist / radius;
		float atten = t * t;
		sum += lc.rgb * lc.w * ndotl * atten;
	}
	return sum;
}

float3 BlinnPhong(SpecLight L, float3 N, float3 V)
{
	float3 H = normalize(V - L.dir.xyz);
	float power = max(L.dir.w, 4.0);
	return pow(saturate(dot(N, H)), power) * L.color.rgb;
}

float4 main(VS_out input) : COLOR
{
	float3 N = normalize(input.WorldNormal);
	float3 V = normalize(eyePosPS - input.WorldPos);
	float NdotV = saturate(dot(N, V));

	// Fresnel — Schlick, lerped toward 1.0 by reflProps.x so the artist
	// can override how grazing-angle the reflection is.
	float f = pow(1.0 - NdotV, 5.0);
	float fresnel = lerp(f, 1.0, reflProps.x);
	float reflStrength = saturate(fresnel * reflProps.z);

	// Per-pixel reflection lookup. The legacy 2D env-map gets a planar
	// unwrap; the new IBL capture cube takes the world-space reflection
	// vector directly — much more accurate at grazing angles. Blend
	// factor (reflProps2.x) lets the host pick how cube-driven the
	// reflection is at runtime.
	float3 R = ReflectV(V, N);
	float2 reflUv = R.xy * 0.5 + 0.5;

	float4 diffuse = input.Color * tex2D(tex0, input.TexCoord0.xy);
	float3 envmapPlanar = tex2D(tex1, reflUv).rgb;
	float3 envmapCube   = texCUBE(iblCaptureCube, R).rgb;
	float3 envmap = lerp(envmapPlanar, envmapCube, saturate(reflProps2.x));

	float3 base = lerp(diffuse.rgb, envmap, reflStrength);

	// Dynamic point lights — host-side selected nearby CPointLights apply
	// to vehicle paint just like to any pp_PS surface. Modulated by the
	// diffuse term so dark-coloured cars don't suddenly glow neon under
	// a streetlamp. Applied before the spec pass so a wet/headlit hood
	// still picks up the BlinnPhong sun spec on top.
	base += ApplyDynamicPointLights(input.WorldPos, N) * diffuse.rgb;

	// Per-pixel specular highlights — five directional spec lights pushed
	// from custompipes_d3d9.cpp::uploadSpecLights.
	float3 spec = float3(0.0, 0.0, 0.0);
	[branch]
	if(reflProps.w > 0.001){
		[unroll(5)]
		for(int i = 0; i < 5; i++)
			spec += BlinnPhong(specLights[i], N, V) * reflProps.w * reflProps.y;
	}

	base = lerp(fogColor.rgb, base, input.TexCoord0.z);

	float4 color;
	color.rgb = base * diffuse.a + spec * input.TexCoord0.z;
	color.a = diffuse.a;
	return color;
}
