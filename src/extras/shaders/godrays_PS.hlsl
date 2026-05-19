// Radial god-rays (volumetric sun shafts).
//
// Input is the already-bright-passed + Gaussian-blurred bloom buffer; we
// sample along the ray from the current pixel toward the screen-space sun
// position, accumulating with exponential decay. The result is composited
// additively on top of the scene by the host so we don't need to read the
// scene texture in this pass.

sampler2D tex0 : register(s0);

float4 godParams : register(c10);
// .xy = sun screen position (0..1)
// .z  = density           (sample spacing scale)
// .w  = decay             (per-sample multiplier, 0.94 typical)

float4 godColor : register(c11);
// .rgb = ray tint
// .w   = exposure (overall scale)

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

#define GODRAY_SAMPLES 32

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float2 sunPos = godParams.xy;

	float2 deltaUv = (uv - sunPos) * (godParams.z / GODRAY_SAMPLES);
	float illum = 1.0;
	float3 accum = float3(0.0, 0.0, 0.0);

	[unroll(GODRAY_SAMPLES)]
	for(int i = 0; i < GODRAY_SAMPLES; i++){
		uv -= deltaUv;
		accum += tex2D(tex0, uv).rgb * illum;
		illum *= godParams.w;
	}
	accum /= GODRAY_SAMPLES;

	return float4(accum * godColor.rgb * godColor.w, 1.0);
}
