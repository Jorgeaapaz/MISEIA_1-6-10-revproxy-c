#pragma once
#include <stdint.h>
#include "log.h"

#define MAX_LISTENERS  16
#define MAX_ROUTES     1024
#define MAX_BACKENDS   64

typedef struct {
    uint16_t port;
    int      tls;
    char     cert[256];
    char     key[256];
} ListenerCfg;

typedef struct {
    char  domain[253];
    char  backends[MAX_BACKENDS][256];
    int   nbackends;
} RouteCfg;

typedef struct {
    int         workers;
    int         connect_timeout_ms;
    int         read_timeout_ms;
    LogLevel    log_level;
    int         forwarded_for;    /* add X-Forwarded-For header */
    ListenerCfg listeners[MAX_LISTENERS];
    int         nlisteners;
    RouteCfg    routes[MAX_ROUTES];
    int         nroutes;
} Config;

/* Load configuration from TOML file at `path` into *out.
 * Returns 0 on success, -1 on parse/validation error. */
int  config_load(const char *path, Config *out);

/* Release any heap resources (currently no heap alloc, but kept for ABI) */
void config_free(Config *cfg);

/* Validate a loaded config. Returns 0 if valid, -1 if not. */
int  config_validate(const Config *cfg);
