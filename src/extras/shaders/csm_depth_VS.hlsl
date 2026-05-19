// Depth-only vertex shader for CSM cascade rendering.
//
// Transforms the world-space vertex by the cascade's light view-projection
// matrix (uploaded each pass) and writes linear normalised depth into
// TexCoord0.x so the PS can store it as R32F.

float4x4 lightViewProj : register(c0);
float4x4 worldMat      : register(c4);

struct VS_in {
	float4 Position : POSITION;
};

struct VS_out {
	float4 Position  : POSITION;
	float  ViewDepth : TEXCOORD0;	// in [0,1] thanks to the ortho projection
};

VS_out main(in VS_in input)
{
	VS_out output;
	float4 worldPos = mul(worldMat, input.Position);
	output.Position = mul(lightViewProj, worldPos);
	// Linear depth as written into the orthographic clip space's z/w.
	output.ViewDepth = saturate(output.Position.z / output.Position.w);
	return output;
}
