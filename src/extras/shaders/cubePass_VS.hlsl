// Minimal pass-through vertex shader used by direct-D3D9 cube-face
// dispatch in CIBL::renderCubeFace. Bypasses librw's im2d path
// entirely so the cube render doesn't depend on engine->currentCamera
// existing yet, and so the screen-space transform doesn't get
// scaled by the scene camera's framebuffer dimensions (which would
// make the quad cover only a tiny fraction of the actual cube face).
//
// Position is already in NDC (-1..1), UV is interpolated to the PS.
// COLOR0 stays opaque white so PS variants reading input.Color don't
// see uninitialised values.

struct VS_in {
	float4 Position : POSITION;
	float2 TexCoord : TEXCOORD0;
};

struct VS_out {
	float4 Position  : POSITION;
	float2 TexCoord0 : TEXCOORD0;
	float4 Color     : COLOR0;
};

VS_out main(VS_in input)
{
	VS_out o;
	o.Position  = input.Position;
	o.TexCoord0 = input.TexCoord;
	o.Color     = float4(1.0, 1.0, 1.0, 1.0);
	return o;
}
