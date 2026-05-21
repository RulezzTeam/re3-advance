// Stage 19 — Volumetric clouds.
//
// Half-res raymarch through a procedural cloud layer between two
// altitudes (host-set, typically 1200..2400 m). Per pixel: if the
// view ray hits the sky (gbuf.a < epsilon), march N steps through
// the layer, sample procedural 3D noise (Worley + Perlin via hash
// approximations — no texture atlas needed for a first pass), and
// accumulate (scatter, transmittance). The hdrResolve pass composes
// the result onto sky pixels with alpha = transmittance.
//
// Bindings:
//   s0 = pGbufNormalDepth (.a = viewZ/farClip; we only consume sky)
// Constants:
//   c10 = camera world pos (.xyz) + farClip (.w)
//   c11..c14 = world frustum corner rays (TL, TR, BR, BL)
//   c15 = sun direction (.xyz) + HG g (.w)
//   c16 = sun colour (HDR-safe rgb) + density multiplier (.w)
//   c17 = layer bottom (.x), layer top (.y), wind X-speed (.z),
//         wind Y-speed (.w)
//   c18 = step count (.x clamped 8..32), time accumulator (.y),
//         coverage 0..1 (.z), reserved (.w)

sampler2D gbufTex : register(s0);
float4 vcCam     : register(c10);
float4 vcRayTL   : register(c11);
float4 vcRayTR   : register(c12);
float4 vcRayBR   : register(c13);
float4 vcRayBL   : register(c14);
float4 vcSun     : register(c15);
float4 vcSunCol  : register(c16);
float4 vcLayer   : register(c17);
float4 vcQuality : register(c18);

struct VS_out {
	float4 Position  : POSITION;
	float2 TexCoord0 : TEXCOORD0;
	float4 Color     : COLOR0;
};

// 2D hash → smooth value noise. Cheap and good enough as the base
// frequency for a cloud field; we layer two octaves for shape +
// detail. Real Worley would tile better but costs 4× the ALU.
float Hash2(float2 p)
{
	p = frac(p * float2(127.1, 311.7));
	p += dot(p, p + 17.31);
	return frac(p.x * p.y);
}

float ValueNoise2(float2 p)
{
	float2 i = floor(p);
	float2 f = frac(p);
	float a = Hash2(i);
	float b = Hash2(i + float2(1, 0));
	float c = Hash2(i + float2(0, 1));
	float d = Hash2(i + float2(1, 1));
	float2 u = f * f * (3.0 - 2.0 * f);
	return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// 3D-ish noise: combine 2D noise at the XY plane with a vertical
// blend that lets clouds taper at the layer top/bottom. Cheaper than
// true 3D noise and gives readable layered cumulus.
float CloudDensity(float3 wp, float windX, float windY, float time)
{
	float2 p = wp.xy * 0.0007 + float2(windX, windY) * time;	// big shapes
	float n1 = ValueNoise2(p);
	float n2 = ValueNoise2(p * 4.3 + 11.0);						// detail
	float shape = n1 * 0.7 + n2 * 0.3;

	// Vertical taper — full strength near layer mid, fades toward bottom
	// and top so the layer doesn't read as a hard slab.
	float h = saturate((wp.z - vcLayer.x) / max(vcLayer.y - vcLayer.x, 1.0));
	float vertical = sin(h * 3.14159);	// 0 at edges, 1 in middle
	return saturate(shape * vertical);
}

float HGPhase(float cosT, float g)
{
	float g2 = g * g;
	float denom = 1.0 + g2 - 2.0 * g * cosT;
	return (1.0 - g2) / (4.0 * 3.14159 * denom * sqrt(max(denom, 1e-4)));
}

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	float4 g = tex2D(gbufTex, uv);
	// Only run on sky pixels (gbuf.a == 0 = no surface rendered). If
	// the pixel has a near-surface, the clouds are behind it — skip.
	if(g.a > 0.0005)
		return float4(0, 0, 0, 1);	// transmittance = 1 → no effect

	float3 ray = lerp(lerp(vcRayTL.xyz, vcRayTR.xyz, uv.x),
	                  lerp(vcRayBL.xyz, vcRayBR.xyz, uv.x),
	                  uv.y);
	float rayLen = length(ray);
	if(rayLen < 1e-4) return float4(0, 0, 0, 1);
	float3 fwd = ray / rayLen;

	// Find entry / exit altitudes on the cloud layer slab.
	float t0 = (vcLayer.x - vcCam.z) / max(abs(fwd.z), 1e-4);
	float t1 = (vcLayer.y - vcCam.z) / max(abs(fwd.z), 1e-4);
	if(fwd.z < 0.0){ float tmp = t0; t0 = t1; t1 = tmp; }
	if(t1 < 0.0) return float4(0, 0, 0, 1);	// layer is behind camera
	t0 = max(t0, 0.0);
	if(t1 - t0 < 1.0) return float4(0, 0, 0, 1);

	int STEPS = (int)clamp(vcQuality.x, 8.0, 32.0);
	float stepLen = (t1 - t0) / (float)STEPS;
	float cosT = dot(fwd, vcSun.xyz);
	float phase = HGPhase(cosT, vcSun.w);

	// Coverage threshold gates which noise values count as "cloud".
	// Lower = patchy clouds, higher = full overcast.
	float covThresh = lerp(0.65, 0.30, vcQuality.z);

	float3 scatter = float3(0, 0, 0);
	float trans = 1.0;
	float jitter = frac(sin(dot(uv * 12.345, float2(12.9898, 78.233))) * 43758.5);

	[loop]
	for(int i = 0; i < 32; i++){
		float doStep = step(float(i), float(STEPS));
		float t = t0 + ((float)i + jitter) * stepLen;
		float3 wp = vcCam.xyz + fwd * t;

		float density = CloudDensity(wp, vcLayer.z, vcLayer.w, vcQuality.y);
		density = saturate((density - covThresh) / (1.0 - covThresh));
		density *= vcSunCol.w;

		float ext = density * stepLen * 0.5;
		// In-scatter: sun colour × phase × density × stepLen, attenuated
		// by current transmittance. Simple single-scatter approximation —
		// no multiple-scatter (would need a second LUT).
		float3 inScat = vcSunCol.rgb * phase * density * stepLen * doStep;
		scatter += inScat * trans;
		trans *= exp(-ext * doStep);
	}

	// Output: (in-scatter rgb, transmittance through cloud layer).
	// Composer in hdrResolve lerps sky × trans + scatter.
	return float4(scatter, trans);
}
