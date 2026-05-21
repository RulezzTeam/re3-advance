// Stage 15.2 — Deferred decal splat shader (placeholder).
//
// Full-screen pass that walks up to 16 active decals and adds each
// one's colour onto pHdrScene where the gbuf depth places the surface
// inside the decal's influence sphere. Cheap (16 × 4 ALU per pixel)
// and runs once per frame between SSGI and ResolveHDR.
//
// Bindings:
//   s0 = pGbufNormalDepth (RGB = world N×0.5+0.5, A = viewZ / farClip)
// Constants:
//   c10..c25 = 16 vec4 (pos.xyz, radius)  — empty slot if radius == 0
//   c26..c41 = 16 vec4 (rgb, alpha)       — alpha fades on age
//   c42      = camera world pos (.xyz) + farClip (.w)
//   c43..c46 = world frustum corner rays (TL, TR, BR, BL)

sampler2D gbufTex : register(s0);
float4 decalPos[16] : register(c10);
float4 decalCol[16] : register(c26);
float4 camPos : register(c42);
float4 rayTL : register(c43);
float4 rayTR : register(c44);
float4 rayBR : register(c45);
float4 rayBL : register(c46);

struct VS_out {
	float4 Position  : POSITION;
	float2 TexCoord0 : TEXCOORD0;
	float4 Color     : COLOR0;
};

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float4 g = tex2D(gbufTex, uv);
	if(g.a < 0.0005)
		return float4(0, 0, 0, 0);	// sky / clear

	float viewZ = g.a * camPos.w;
	float3 ray = lerp(lerp(rayTL.xyz, rayTR.xyz, uv.x),
	                  lerp(rayBL.xyz, rayBR.xyz, uv.x),
	                  uv.y);
	float3 wp = camPos.xyz + ray * viewZ;

	float3 acc = float3(0, 0, 0);
	float accA = 0.0;
	// Manual unroll — fxc unrolls fine but we'd rather make it explicit
	// so ALU cost is predictable across drivers.
	[unroll]
	for(int i = 0; i < 16; i++){
		float r = decalPos[i].w;
		if(r < 0.001) continue;
		float3 d = decalPos[i].xyz - wp;
		float d2 = dot(d, d);
		float r2 = r * r;
		if(d2 > r2) continue;
		// Smoothstep falloff: 1 at centre → 0 at edge, squared.
		float t = 1.0 - saturate(d2 / r2);
		float w = t * t * decalCol[i].a;
		acc  += decalCol[i].rgb * w;
		accA += w;
	}
	return float4(acc, saturate(accA));
}
