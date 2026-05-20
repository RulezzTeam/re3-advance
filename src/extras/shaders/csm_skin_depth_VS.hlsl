// CSM depth-only vertex shader for skinned meshes (peds + drivers).
//
// Applies the bone matrices to the input Position before transforming
// through worldMat → lightViewProj so peds cast correctly-deformed
// shadow silhouettes instead of T-pose blocks. Same register layout as
// csm_depth_VS plus c41..c233 used by the skinning matrices (matches
// librw's skin_VS / skin_pp_VS / skin_pp_gbuf_VS register allocation).

float4x4 lightViewProj : register(c0);
float4x4 worldMat      : register(c4);
float4x3 boneMatrices[64] : register(c41);

struct VS_in
{
	float4 Position		: POSITION;
	float3 Normal		: NORMAL;
	float2 TexCoord		: TEXCOORD0;
	float4 Prelight		: COLOR0;
	float4 Weights		: BLENDWEIGHT;
	int4   Indices		: BLENDINDICES;
};

struct VS_out
{
	float4 Position  : POSITION;
	float  ViewDepth : TEXCOORD0;	// matches csm_depth_PS — receives the cascade clip-space z
};

VS_out main(in VS_in input)
{
	VS_out output;

	// Skin the position through the bone palette — same loop the regular
	// skin VS does, just without the normal/tangent/world-position outputs
	// the colour pass needs.
	float3 SkinVertex = float3(0.0, 0.0, 0.0);
	[unroll]
	for(int j = 0; j < 4; j++){
		SkinVertex += mul(input.Position, boneMatrices[input.Indices[j]]).xyz * input.Weights[j];
	}

	// worldMat × skinned vertex → world space → lightViewProj → clip.
	float4 worldPos = mul(worldMat, float4(SkinVertex, 1.0));
	output.Position = mul(lightViewProj, worldPos);
	output.ViewDepth = saturate(output.Position.z / output.Position.w);
	return output;
}
