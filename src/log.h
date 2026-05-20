#pragma once
#include <stdint.h>
#include <stdio.h>

typedef enum {
    LOG_TRACE = 0,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;

/* Initialize the logging subsystem.
 * path == NULL → write to stderr.
 * Returns 0 on success, -1 on error. */
int  log_init(const char *path, LogLevel level);

/* Change the minimum log level at runtime (used after config reload) */
void log_set_level(LogLevel level);

/* Flush and close the log file (if any) */
void log_close(void);

/* Get current log file pointer (always valid after log_init) */
FILE *log_file(void);

/* Low-level emit – prefer the macros below */
void log_emit(LogLevel level, const char *file, int line,
              const char *fmt, ...);

/* Convenience macros */
#define LOG_TRACE(...) log_emit(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) log_emit(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  log_emit(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  log_emit(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_emit(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
