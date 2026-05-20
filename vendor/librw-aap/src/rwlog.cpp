#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "rwlog.h"

namespace rw {

static FILE       *g_logFile  = nullptr;
static RwLogLevel  g_logLevel = RW_LOG_INFO;

static const char *
levelTag(RwLogLevel l)
{
	switch(l){
	case RW_LOG_TRACE: return "TRACE";
	case RW_LOG_INFO:  return "INFO ";
	case RW_LOG_WARN:  return "WARN ";
	case RW_LOG_ERROR: return "ERROR";
	}
	return "?    ";
}

static void
formatTimestamp(char *buf, size_t bufSize)
{
	// HH:MM:SS.mmm — local time. We don't need date because the log
	// header line includes it (written once at rwLogInit time).
#ifdef _WIN32
	SYSTEMTIME st;
	GetLocalTime(&st);
	_snprintf_s(buf, bufSize, _TRUNCATE, "%02u:%02u:%02u.%03u",
	            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
	time_t now = time(nullptr);
	struct tm *lt = localtime(&now);
	if(lt)
		strftime(buf, bufSize, "%H:%M:%S.000", lt);
	else
		strncpy(buf, "00:00:00.000", bufSize);
#endif
}

void
rwLogInit(const char *filename)
{
	rwLogShutdown();
	if(filename == nullptr)
		return;
	g_logFile = fopen(filename, "w");
	if(g_logFile == nullptr)
		return;

	// Header — one-time, includes wall-clock date and start time so a
	// log-after-the-fact reader knows which session this file belongs to.
#ifdef _WIN32
	SYSTEMTIME st;
	GetLocalTime(&st);
	fprintf(g_logFile,
	    "=== reVC graphics log — opened %04u-%02u-%02u %02u:%02u:%02u ===\n",
	    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
#else
	time_t now = time(nullptr);
	struct tm *lt = localtime(&now);
	if(lt){
		fprintf(g_logFile,
		    "=== reVC graphics log — opened %04d-%02d-%02d %02d:%02d:%02d ===\n",
		    lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
		    lt->tm_hour, lt->tm_min, lt->tm_sec);
	}
#endif
	fflush(g_logFile);
}

void
rwLogShutdown(void)
{
	if(g_logFile){
		fclose(g_logFile);
		g_logFile = nullptr;
	}
}

void
rwLogSetLevel(RwLogLevel level)
{
	g_logLevel = level;
}

void
rwLogf(RwLogLevel level, const char *fmt, ...)
{
	if(level < g_logLevel)
		return;

	char ts[16];
	formatTimestamp(ts, sizeof(ts));

	char msg[1024];
	va_list args;
	va_start(args, fmt);
#ifdef _WIN32
	_vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, args);
#else
	vsnprintf(msg, sizeof(msg), fmt, args);
#endif
	va_end(args);

	// File output — auto-flushed so a crash mid-frame doesn't lose
	// the last few records.
	if(g_logFile){
		fprintf(g_logFile, "[%s] [%s] %s\n", ts, levelTag(level), msg);
		fflush(g_logFile);
	}

	// VS debugger output. OutputDebugString uses one Win32 call per
	// line and is mostly free when no debugger is attached.
#ifdef _WIN32
	char dbg[1280];
	_snprintf_s(dbg, sizeof(dbg), _TRUNCATE, "[%s] [%s] %s\n", ts, levelTag(level), msg);
	OutputDebugStringA(dbg);
#endif
}

}
