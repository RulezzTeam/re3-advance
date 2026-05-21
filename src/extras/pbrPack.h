#pragma once

#ifdef POSTFX_HDR

#include "common.h"

// Stage 13 — Optional PBR via neo_pbr.txd asset pack.
//
// STRICTLY OPT-IN. The plan calls for three nested levels of guarding:
//   1. Global toggle (CPostFX::PbrEnable) — default false. When off,
//      zero PBR code paths execute.
//   2. Asset-pack availability — even with PbrEnable=true, if the
//      `models/neo_pbr.txd` file is absent on disk the engine logs
//      a one-time warning and downgrades to legacy at runtime. Game
//      runs identically to PbrEnable=false. Menu shows "(asset pack
//      missing)" suffix on the toggle so the user knows.
//   3. Per-material fallback — materials NOT present in the pack
//      keep their legacy Blinn-Phong lighting. No magenta, no checker,
//      no warnings per frame. Production assets gradually migrate
//      into the pack; the engine doesn't force anything.
//
// This first-pass landing implements the AVAILABILITY check + menu
// gating. The actual TXD loading + material map lookups + G-buffer
// slot 2 (metallic+roughness+spec+AO) + receiver PBR math come in
// Stage 13.2 once an asset pack exists to test against.

class CPbrPack
{
public:
	// True after Open() finds + loads neo_pbr.txd. The menu uses this
	// to grey out the PBR toggle with an explanatory label when the
	// asset pack is missing — strict opt-in: no PBR code path executes
	// when this is false.
	static bool PackAvailable;

	// Try to load `models/neo_pbr.txd`. Idempotent — safe to call
	// multiple times. Returns true if the pack is now loaded, false
	// if it doesn't exist (with a single log line so the warning
	// doesn't spam).
	static bool Open(void);
	// Free TXD + reset PackAvailable to false.
	static void Close(void);

	// Menu CCFOSelect AfterChange handler — triggers Open() when the
	// user toggles PbrEnable ON, so the warning (or success log) hits
	// the moment they ask for it instead of waiting for a render.
	static void PbrEnableAfterChange(int8 before, int8 after);
};

#endif
