#include "common.h"

#ifdef POSTFX_HDR

#include "main.h"
#include "FileMgr.h"
#include "postfx.h"
#include "pbrPack.h"

bool CPbrPack::PackAvailable = false;

static bool sWarningLogged = false;

bool
CPbrPack::Open(void)
{
	if(PackAvailable) return true;	// already loaded

	// The asset pack lives at models/neo_pbr.txd next to the standard
	// TXD files. Check existence via FileMgr instead of a full TXD
	// load so the gate is cheap even if every frame asks.
	int fh = CFileMgr::OpenFile("models\\neo_pbr.txd", "rb");
	if(fh == 0){
		if(!sWarningLogged){
			debug("PBR: models/neo_pbr.txd not found — PBR disabled at runtime\n");
			sWarningLogged = true;
		}
		PackAvailable = false;
		return false;
	}
	CFileMgr::CloseFile(fh);

	// File exists. The actual TXD parse + per-material map indexing
	// is Stage 13.2 — for now we set PackAvailable=true so the menu
	// toggle can light up. The receiver shaders DO NOT consume the
	// pack yet, so even with PackAvailable=true the legacy lighting
	// path runs unchanged. This keeps "PbrEnable=true && pack found"
	// safe before the rest of the pipeline lands.
	debug("PBR: models/neo_pbr.txd found — scaffold landed, full pipeline deferred to Stage 13.2\n");
	PackAvailable = true;
	return true;
}

void
CPbrPack::Close(void)
{
	PackAvailable = false;
	sWarningLogged = false;	// allow a fresh warning if Open is retried
}

void
CPbrPack::PbrEnableAfterChange(int8 before, int8 after)
{
	(void)before;
	if(after && !PackAvailable){
		// User just turned PBR on — probe the asset pack so the menu
		// can show the correct state on the next redraw. If the file
		// isn't there, debug() fires once + PackAvailable stays false
		// + the gate keeps PBR effectively off downstream.
		Open();
		// If the pack genuinely isn't there, force PbrEnable back to
		// false so subsequent menu interactions stay coherent (the
		// user doesn't see "PBR=on, no asset pack" as a contradictory
		// state). The plan calls this "graceful downgrade".
		if(!PackAvailable){
			CPostFX::PbrEnable = false;
		}
	}
}

#endif
