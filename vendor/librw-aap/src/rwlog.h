#ifndef RW_LOG_H
#define RW_LOG_H

// Lightweight graphics-side log used to surface D3D9 init / raster /
// shader / cube state and any caps-driven feature gating. Backed by a
// file in the working directory (default `reVC-graphics.log`) plus
// OutputDebugString on Windows so the messages also show up in the
// VS debugger's Output pane.
//
// The log is silent until rwLogInit() succeeds (e.g. file couldn't be
// opened) — every helper short-circuits cheaply so adding rwLogf()
// calls everywhere has near-zero cost when the log is disabled.
//
// Threading: not thread-safe. librw + reVC are single-threaded for
// rendering, so a mutex would only buy us nothing. If that ever
// changes, wrap rwLogf with a CriticalSection.

namespace rw {

enum RwLogLevel {
	RW_LOG_TRACE = 0,	// very chatty — per-frame state
	RW_LOG_INFO  = 1,	// once-per-startup / once-per-event status
	RW_LOG_WARN  = 2,	// recoverable abnormality (caps missing, fallback used)
	RW_LOG_ERROR = 3,	// failed D3D9 call, missing resource, etc.
};

// Open `filename` for writing. Pass nullptr to disable. Safe to call
// multiple times — re-opens with the new filename.
void rwLogInit(const char *filename);

// Close the file handle. Subsequent rwLogf calls are no-ops.
void rwLogShutdown(void);

// Below this level, messages are dropped. Default = RW_LOG_INFO.
void rwLogSetLevel(RwLogLevel level);

// Format + write. The output line is timestamped HH:MM:SS.mmm + level
// tag + the formatted message + newline. Auto-flushed so an immediate
// crash doesn't swallow the last few entries.
void rwLogf(RwLogLevel level, const char *fmt, ...);

}

#endif
