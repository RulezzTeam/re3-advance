// Depth-only pixel shader for CSM cascade rendering.
//
// Output channel layout (F16_RGBA cascade RT):
//   .r = z      — linear ortho-space depth (PCF: Sharp/Soft/Ultra read this)
//   .g = z²    — second moment (VSM Chebyshev reads .rg)
//   .b = z³    — third moment (MSM simplified bound reads .rgba — Stage 29)
//   .a = z⁴    — fourth moment (MSM)
//
// z is normalised to ortho cascade range so z ∈ [0,1] over the cascade
// extent. That keeps z² .. z⁴ within F16's ~6.5e4 range (max value
// z⁴ = 1.0 at the far edge) without overflow. EVSM (Stage 28) needs
// exp(80×z) which overflows F16 — that path uses F32_RGBA instead and
// runs through a separate depth caster.
//
// All readers cost the same: a single tex2D fetch. .r-only consumers
// pay nothing extra for the .gba writes. The caster cost is 3 extra
// muls per pixel, vanishingly cheap.

struct VS_out {
	float4 Position  : POSITION;
	float  ViewDepth : TEXCOORD0;
};

float4 main(VS_out input) : COLOR
{
	float z  = input.ViewDepth;
	float z2 = z  * z;
	float z3 = z2 * z;
	float z4 = z2 * z2;
	return float4(z, z2, z3, z4);
}
