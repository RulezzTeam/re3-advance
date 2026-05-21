// EVSM (Exponential Variance Shadow Maps) depth caster — Stage 28.
//
// Standard EVSM-4 formulation (Lauritzen 2008): warp the linear depth z
// through two exponentials (positive and negative) and store both the
// warped value and its square. Receiver runs Chebyshev's inequality on
// each warp independently and takes the larger occlusion estimate (the
// pessimistic one), which heavily reduces the light-bleeding artefact
// that plain VSM/MSM suffer from at high-contrast occluder depth ranges.
//
// Output channel layout (F32_RGBA cascade RT — Stage 27 enabled the format):
//   .r = e_pos   = exp(+kPos * z)
//   .g = e_neg   = -exp(-kNeg * z)
//   .b = e_pos² = exp(+kPos * z)²   — second moment for positive warp
//   .a = e_neg² = exp(-kNeg * z)²   — second moment for negative warp
//
// `kPos` and `kNeg` chosen so the largest possible warped value fits the
// F32 range comfortably (max ≈ exp(2 * kPos) ≈ 4.85e17 at z=1, kPos=20 —
// well below F32's 3.4e38 ceiling). Using equal kPos = kNeg = 20 keeps
// the receiver symmetric and easier to reason about; with the moment²
// stored we get back to value² without any extra ALU at receive time.
//
// Cost: 4 exp() + 1 mul per pixel — vanishing on top of the rasteriser.
// Far cheaper than re-running the cascade pass per filter mode (which
// would be required if EVSM allocated its own RT slot).
//
// Caster cannot be used with F16_RGBA — exp(20*1) ≈ 4.85e8 overflows
// F16's ~6.5e4 cap. CCSM::Open switches to F32_RGBA when SoftnessMode
// is EVSM (4) or Hybrid (6, future); other modes keep F16 for the
// memory budget.

struct VS_out {
	float4 Position  : POSITION;
	float  ViewDepth : TEXCOORD0;	// linear ortho-space depth ∈ [0,1]
};

float4 main(VS_out input) : COLOR
{
	float z = saturate(input.ViewDepth);
	const float kPos = 20.0;
	const float kNeg = 20.0;
	float ePos = exp( kPos * z);
	float eNeg = exp(-kNeg * z);
	// Store -eNeg in .g so the receiver's "darker means farther" sign
	// convention matches across both warps without per-mode branching.
	return float4(ePos, -eNeg, ePos * ePos, eNeg * eNeg);
}
