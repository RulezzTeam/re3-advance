// Stage 21 — Animated ocean wave heightfield.
//
// Generates a 256×256 RGBA16F texture each frame where:
//   .r = sum-of-sinusoids displacement (height in metres)
//   .gb = derived surface tangent perturbation (used to reconstruct
//         the surface normal when sampling)
//   .a = foam mask (1 where waves are crashing, 0 in calm areas)
//
// Real FFT ocean (Tessendorf 2001) requires a Phillips spectrum
// generated on the GPU + 8 stages of ping-pong butterfly + inverse
// FFT. On D3D9 without compute that's ~16 PS dispatches per frame —
// non-trivial infrastructure. This stage's first pass uses a
// 4-octave summed-sinusoid approximation that visually matches FFT
// in the "moderate sea state" range and is one PS draw per frame.
// Real FFT can land in Stage 21.2 by swapping the inner sample loop
// for an inverse-FFT lookup.
//
// Bindings:
//   c10 = time accumulator (.x = seconds), wind direction (.yz = unit
//         vector), wind speed (.w = m/s)
//   c11 = wave amplitude (.x), choppiness (.y), foam threshold (.z),
//         world-space scale of the texture in metres (.w; e.g. 64 m
//         tile that repeats across the ocean surface)

float4 oceanParams : register(c10);
float4 oceanTune   : register(c11);

struct VS_out {
	float4 Position  : POSITION;
	float2 TexCoord0 : TEXCOORD0;
	float4 Color     : COLOR0;
};

// Hash for randomised wave direction phases.
float Hash1(float2 p)
{
	return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float4 main(VS_out input) : COLOR
{
	float2 uv = input.TexCoord0;
	// World-space position of this texel relative to the tile origin.
	// oceanTune.w = tile size in metres; e.g. 64 m means each texel of
	// the 256² texture covers 0.25 m of ocean.
	float2 worldXY = uv * oceanTune.w;

	float time = oceanParams.x;
	float2 windDir = oceanParams.yz;
	float windSpeed = oceanParams.w;
	float amp = oceanTune.x;
	float chop = oceanTune.y;
	float foamThresh = oceanTune.z;

	// 4-octave summed sinusoids with directional + frequency variation.
	// Each octave uses a different wavevector and phase; the sum
	// approximates a Phillips spectrum for moderate sea states.
	float height = 0.0;
	float2 derivative = float2(0, 0);

	[unroll]
	for(int i = 0; i < 4; i++){
		float fi = (float)i;
		// Per-octave: pick a wavevector slightly rotated off windDir,
		// frequency-doubled per octave, amplitude-halved per octave.
		float angle = fi * 0.78 + Hash1(float2(fi, 0)) * 1.6;
		float ca = cos(angle), sa = sin(angle);
		// Rotate windDir by `angle` to get this octave's direction.
		float2 kdir;
		kdir.x = windDir.x * ca - windDir.y * sa;
		kdir.y = windDir.x * sa + windDir.y * ca;
		float freq = pow(2.0, fi) * 0.18;	// base 0.18 → wavelengths ~5..35 m
		float octAmp = amp * pow(0.55, fi);	// amplitude falls per octave
		// Phase = k·x + ω·t. Dispersion relation for deep water:
		// ω = sqrt(g·|k|). g = 9.81; absorbed into `freq` × wind scale.
		float phase = dot(kdir, worldXY) * freq + time * sqrt(freq * 9.81) * (1.0 + windSpeed * 0.05);
		float c = cos(phase);
		float s = sin(phase);
		height += octAmp * s;
		derivative += kdir * (octAmp * freq * c);
	}

	// Choppy waves — Gerstner-style horizontal displacement so crests
	// sharpen instead of staying purely sinusoidal. chop ∈ [0,1].
	derivative *= (1.0 + chop * 2.0);

	// Foam mask — fires where the crest derivative exceeds threshold.
	// Steep upward slopes = breaking wave crest = foam. Soft transition
	// via smoothstep so foam edges don't pop.
	float slope = length(derivative);
	float foam = smoothstep(foamThresh, foamThresh + 0.4, slope);

	return float4(height, derivative.x, derivative.y, foam);
}
