// Glass / water refraction compose pass.
//
// Samples the HDR scene at a UV that has been displaced by the surface
// normal of the refractive object — the classic "screen-space refraction"
// trick. Cheap (1 RT read + 1 normal lookup) and works in d3d9 ps_3_0.
//
// Bindings:
//   s0 = pHdrScene (RGBA16F current frame colour — gives us the source
//        colour BEHIND the refracting surface, since the refracting
//        surface itself is drawn AFTER the opaque pass into the same RT
//        but transparent objects compose on top)
//   s1 = pGbufNormalDepth (slot 1 normal + depth from the opaque pass)
//
// Constants:
//   c10: .x = refraction strength (UV displacement scale; 0.03..0.08 typical)
//        .y = chromatic dispersion (0 = none, 0.005 = mild rainbow)
//        .z = depth attenuation (refraction fades when geometry is close
//             to the camera — kills bad self-refraction at silhouette)
//        .w = enable strength (0 = bypass, 1 = full)

sampler2D hdrTex  : register(s0);
sampler2D gbufTex : register(s1);

float4 refractParams : register(c10);

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float4 gbuf = tex2D(gbufTex, uv);

	// Sky / un-rendered pixels — pass through.
	if(gbuf.a < 0.0005){
		return tex2D(hdrTex, uv);
	}

	float3 N = normalize(gbuf.rgb * 2.0 - 1.0);

	// Screen-space refraction: bend the lookup UV by the X/Y component of
	// the world-space normal. Z is "into screen" so doesn't move the UV.
	// Depth attenuation: closer surfaces refract less so the silhouette
	// stays crisp.
	float depthAtten = saturate(1.0 - gbuf.a * refractParams.z);
	float2 offset = N.xy * refractParams.x * depthAtten * refractParams.w;

	float3 col;
	if(refractParams.y > 0.001){
		// Chromatic dispersion — sample R, G, B at slightly different UV
		// offsets. Like a prism breaking light.
		float disp = refractParams.y * depthAtten;
		float2 dirR = offset * (1.0 + disp);
		float2 dirG = offset;
		float2 dirB = offset * (1.0 - disp);
		col.r = tex2D(hdrTex, uv + dirR).r;
		col.g = tex2D(hdrTex, uv + dirG).g;
		col.b = tex2D(hdrTex, uv + dirB).b;
	}else{
		col = tex2D(hdrTex, uv + offset).rgb;
	}

	return float4(col, 1.0);
}
