#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <process.h>
  typedef DWORD thread_id_t;
  static thread_id_t current_thread_id(void) { return GetCurrentThreadId(); }
#else
  #include <pthread.h>
  typedef unsigned long thread_id_t;
  static thread_id_t current_thread_id(void) {
      return (unsigned long)pthread_self();
  }
#endif

/* ── Global state ─────────────────────────────────────────────────────────── */

static FILE    *g_log_fp    = NULL;
static LogLevel g_log_level = LOG_INFO;
static int      g_own_fp    = 0;   /* 1 if we opened the file and must close */

/* Simple spinlock for log serialization */
#ifdef _WIN32
  static CRITICAL_SECTION g_lock;
  static int               g_lock_init = 0;
  static void lock(void)   { EnterCriticalSection(&g_lock); }
  static void unlock(void) { LeaveCriticalSection(&g_lock); }
#else
  #include <pthread.h>
  static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
  static void lock(void)   { pthread_mutex_lock(&g_lock); }
  static void unlock(void) { pthread_mutex_unlock(&g_lock); }
#endif

/* ── log_init ─────────────────────────────────────────────────────────────── */

int log_init(const char *path, LogLevel level)
{
#ifdef _WIN32
    if (!g_lock_init) {
        InitializeCriticalSection(&g_lock);
        g_lock_init = 1;
    }
#endif
    g_log_level = level;

    if (path && path[0] != '\0') {
        FILE *fp = fopen(path, "a");
        if (!fp) return -1;
        g_log_fp  = fp;
        g_own_fp  = 1;
    } else {
        g_log_fp = stderr;
        g_own_fp = 0;
    }
    return 0;
}

/* ── log_set_level ────────────────────────────────────────────────────────── */

void log_set_level(LogLevel level)
{
    g_log_level = level;
}

/* ── log_close ────────────────────────────────────────────────────────────── */

void log_close(void)
{
    if (g_own_fp && g_log_fp) {
        fflush(g_log_fp);
        fclose(g_log_fp);
        g_log_fp = NULL;
        g_own_fp = 0;
    }
}

/* ── log_file ─────────────────────────────────────────────────────────────── */

FILE *log_file(void)
{
    return g_log_fp ? g_log_fp : stderr;
}

/* ── level_str ────────────────────────────────────────────────────────────── */

static const char *level_str(LogLevel l)
{
    switch (l) {
        case LOG_TRACE: return "TRACE";
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO ";
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
        default:        return "?????";
    }
}

/* ── iso8601_now ──────────────────────────────────────────────────────────── */

static void iso8601_now(char *buf, size_t len)
{
#ifdef _WIN32
    SYSTEMTIME st;
    GetSystemTime(&st);
    snprintf(buf, len,
             "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             st.wYear, st.wMonth,  st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_val;
    gmtime_r(&ts.tv_sec, &tm_val);
    int ms = (int)(ts.tv_nsec / 1000000);
    snprintf(buf, len,
             "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
             tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec, ms);
#endif
}

/* ── log_emit ─────────────────────────────────────────────────────────────── */

void log_emit(LogLevel level, const char *file, int line,
              const char *fmt, ...)
{
    if (level < g_log_level) return;

    char ts[32];
    iso8601_now(ts, sizeof(ts));

    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    FILE *fp = g_log_fp ? g_log_fp : stderr;

    lock();
    fprintf(fp, "%s [%s] [tid:%lu] %s:%d %s\n",
            ts, level_str(level), (unsigned long)current_thread_id(),
            file, line, msg);
    fflush(fp);
    unlock();
}
