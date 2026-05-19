// Depth-only pixel shader for CSM cascade rendering.
//
// Outputs the linear ortho-space depth into R, which the receiver
// (default_PS with SHADOWS_CSM enabled) will compare against the
// receiver pixel's projected light-space z.

struct VS_out {
	float4 Position  : POSITION;
	float  ViewDepth : TEXCOORD0;
};

float4 main(VS_out input) : COLOR
{
	return float4(input.ViewDepth, 1.0, 1.0, 1.0);
}
