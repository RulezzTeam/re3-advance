// Depth-only pixel shader for CSM cascade rendering.
//
// Outputs:
//   .r = linear ortho-space depth (used by PCF receivers — Sharp/Soft/Ultra)
//   .g = depth² (used by the VSM receiver — Chebyshev inequality)
// The two channels coexist in the same F16_RGBA cascade RT; PCF readers
// only sample .r and pay nothing extra for the .g write. One mul per
// caster pixel is the entire VSM authoring cost — no extra RT, no
// separate render pass.

struct VS_out {
	float4 Position  : POSITION;
	float  ViewDepth : TEXCOORD0;
};

float4 main(VS_out input) : COLOR
{
	float z = input.ViewDepth;
	return float4(z, z*z, 1.0, 1.0);
}
