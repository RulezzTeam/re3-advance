// Screen-space ambient occlusion.
//
// Reads the packed G-buffer (CGBuffer::pGbufNormalDepth):
//   RGB = world-space normal * 0.5 + 0.5
//   A   = linear depth (viewZ / farClip) in [0,1]
//
// 16-tap hemisphere sampling oriented by the per-pixel normal. The kernel
// is uploaded from the host as a unit-length hemisphere; we reflect it
// through a per-pixel random vector sampled from the noise tile so two
// neighbouring fragments don't share the same sample pattern.
//
// Output: AO factor in R channel, 1.0 = fully lit, 0.0 = fully occluded.

sampler2D gbufTex   : register(s0);	// normal + depth
sampler2D noiseTex  : register(s1);	// 4x4 RGBA random rotations

// .x = sample radius (world units)
// .y = depth bias (avoids self-occlusion on planar surfaces)
// .z = intensity scale (0..2)
// .w = far clip (used to recover view-space Z from packed depth)
float4 ssaoParams : register(c10);

// .x = camera-tex 1/width, .y = camera-tex 1/height, .z = noise tile scale,
// .w = AO range falloff
float4 ssaoTexel : register(c11);

// 16 sample directions on a unit hemisphere (z >= 0), pre-scattered with
// a quadratic falloff so most samples hug the origin and a few reach the
// outer ring. Uploaded once at init.
float4 ssaoKernel[16] : register(c16);

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float4 gbuf = tex2D(gbufTex, uv);
	float3 N = gbuf.rgb * 2.0 - 1.0;
	float  depth = gbuf.a;

	// Early-out: pixels with zero packed normal (e.g. sky writes nothing
	// to the G-buffer) shouldn't be occluded. Sky alpha == 0 too.
	if(depth < 0.0001 || dot(N, N) < 0.01)
		return float4(1.0, 1.0, 1.0, 1.0);

	// Random rotation from the noise tile — sample at a higher-frequency
	// UV so the 4x4 tile actually tiles across the screen.
	float3 rnd = tex2D(noiseTex, uv * ssaoTexel.z).xyz * 2.0 - 1.0;
	rnd.z = 0;

	// Build TBN from world-space normal + random tangent — classic Crytek
	// orientation step.
	float3 T = normalize(rnd - N * dot(rnd, N));
	float3 B = cross(N, T);
	float3x3 TBN = float3x3(T, B, N);

	float radius = ssaoParams.x;
	float bias   = ssaoParams.y;
	float intensity = ssaoParams.z;
	float farClip = ssaoParams.w;

	// Reconstruct view-space depth from packed depth.
	float centreZ = depth * farClip;

	float occlusion = 0.0;
	[unroll(16)]
	for(int i = 0; i < 16; i++){
		// Hemisphere sample rotated by TBN, scaled by radius (world units).
		float3 sampleDir = mul(ssaoKernel[i].xyz, TBN);
		// Approximate world->screen offset by treating sampleDir.xy as a
		// view-aligned offset and using sampleDir.z as a depth bias. This
		// is the standard cheap-SSAO trick that avoids needing the full
		// projection matrix here.
		float2 sampleUv = uv + sampleDir.xy * radius * ssaoTexel.xy * 64.0;
		float  sampleZ  = centreZ + sampleDir.z * radius;

		float4 hit = tex2D(gbufTex, sampleUv);
		float  hitZ = hit.a * farClip;

		// Range check: only count occluders that are close in Z — distant
		// pixels behind the sample point shouldn't darken the centre.
		float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(centreZ - hitZ), 0.001));

		// Occluded if the sampled depth is closer to the camera than our
		// hemisphere sample point (plus a small bias to suppress acne).
		occlusion += (hitZ <= sampleZ - bias ? 1.0 : 0.0) * rangeCheck;
	}

	float ao = 1.0 - (occlusion / 16.0) * intensity;
	return float4(saturate(ao), 1.0, 1.0, 1.0);
}
