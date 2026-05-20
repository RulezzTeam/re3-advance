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

// Contact AO — short screen-space ray-march catching sub-pixel occluders
// the hemisphere kernel misses (foot-to-ground, tyre-to-road, ped-to-car
// shoulder). 4 unit-cross taps at a tight radius; rejects matches outside
// a small Z-window so we don't darken distant geometry.
// .x = strength (0 disables — early-out the [branch])
// .y = pixel radius (in screen-space texels, ~2..6)
// .z = max Z delta in metres (anything farther doesn't count)
// .w = bias multiplier on the inner Z reject
float4 ssaoContact : register(c12);

// Contact Shadows — a separate, longer screen-space ray-march biased
// toward the sun direction. Captures fine micro-occlusion that the
// 4-tap CSM PCF doesn't resolve (door-frames, wrinkles in clothes,
// crevices on terrain). Output is multiplied into the AO term so it
// inherits the SSAO compose path in hdrResolve_PS.
// .xyz = sun direction projected into screen space (uv-delta per step)
// .w   = strength (0 = off)
float4 ssaoContactShadow : register(c13);
// .x = step count (4..16), .y = thickness in metres, .z = bias, .w = unused
float4 ssaoContactShadowTuning : register(c14);

// 16 sample directions on a unit hemisphere (z >= 0), pre-scattered with
// a quadratic falloff so most samples hug the origin and a few reach the
// outer ring. Uploaded once at init.
float4 ssaoKernel[16] : register(c16);

struct VS_out {
	float4 Position		: POSITION;
	float2 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

// NaN-safe normalize — uninit gbuf pixels can read as (0,0,0,0); the
// subsequent normalize() returns NaN and AO writes a black ring around
// the artifact. Skip-on-empty is the primary defense; this is the
// belt-and-braces for borderline-valid normals.
float3 SafeNormalize(float3 v)
{
	float l2 = dot(v, v);
	return v * rsqrt(max(l2, 1e-8));
}

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

	N = SafeNormalize(N);

	// Random rotation from the noise tile — sample at a higher-frequency
	// UV so the 4x4 tile actually tiles across the screen.
	float3 rnd = tex2D(noiseTex, uv * ssaoTexel.z).xyz * 2.0 - 1.0;
	rnd.z = 0;

	// Build TBN from world-space normal + random tangent — classic Crytek
	// orientation step.
	float3 T = SafeNormalize(rnd - N * dot(rnd, N));
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

	// Contact AO — 4-tap unit-cross ray-march. Each tap is offset by a
	// small number of texels and checks whether the gbuf depth in that
	// direction is in front of us by a small amount. Catches the dark
	// hairline you get where two near-tangential surfaces meet (shoes on
	// road, tyres at the kerb, ped pressed against a car door).
	//
	// We unroll the loop and weight the result by ssaoContact.x. When the
	// strength is 0 the contribution multiplies out to nothing — no
	// [branch] needed (ps_3_0 doesn't allow branch+gradient-tex reads in
	// the same scope without uniform UVs).
	{
		const float2 contactDirs[4] = {
			float2( 1.0,  0.0), float2(-1.0,  0.0),
			float2( 0.0,  1.0), float2( 0.0, -1.0)
		};
		float pixRadius = ssaoContact.y;
		float maxDz    = ssaoContact.z;
		float innerBias = ssaoContact.w;

		float contact = 0.0;
		[unroll(4)]
		for(int j = 0; j < 4; j++){
			float2 cuv = uv + contactDirs[j] * pixRadius * ssaoTexel.xy;
			float chitZ = tex2D(gbufTex, cuv).a * farClip;
			float dz = centreZ - chitZ;	// positive = neighbour is closer
			// Reject too-tiny (self) and too-large (background) deltas.
			float ok = step(bias * innerBias, dz) * step(dz, maxDz);
			// Weight by how mid-range the delta is.
			contact += ok * (1.0 - saturate(dz / maxDz));
		}
		ao -= ssaoContact.x * (contact * 0.25);
	}

	// Contact shadows — march toward the projected sun direction in
	// screen space. At each step, if the depth there is *closer* than
	// our expected ray depth, we have an occluder between us and the
	// sun. Strength multiplies the result; ssaoContactShadow.xyz is the
	// per-step UV+Z increment (sun-aligned in screen space).
	{
		const int   csSteps   = (int)max(ssaoContactShadowTuning.x, 1.0);
		const float csThick   = ssaoContactShadowTuning.y;
		const float csBias    = ssaoContactShadowTuning.z;
		float occShadow = 0.0;
		float alive = 1.0;
		[unroll(16)]
		for(int i = 0; i < 16; i++){
			float doStep = step(float(i), float(csSteps - 1)) * alive;
			float t = float(i + 1);
			float2 sUv  = uv + ssaoContactShadow.xy * t;
			float  sZ   = centreZ + ssaoContactShadow.z * t * csThick;
			float  hitZ = tex2D(gbufTex, sUv).a * farClip;
			float  dz   = sZ - hitZ;
			// Inside the window: occluder found. dz > 0 means we passed
			// behind something closer to the camera.
			float thisHit = step(csBias, dz) * step(dz, csThick) * doStep;
			occShadow = max(occShadow, thisHit);
			// Stop marching once we've hit (mute further iterations).
			alive *= 1.0 - thisHit;
		}
		ao -= ssaoContactShadow.w * occShadow;
	}

	return float4(saturate(ao), 1.0, 1.0, 1.0);
}
