#pragma once
#include "config.h"
#include <stdatomic.h>

typedef struct {
    _Atomic int idx;
} RouteCounter;

typedef struct {
    RouteCounter counters[MAX_ROUTES];
} Balancer;

/* Allocate a new balancer for nroutes routes (all counters start at 0). */
Balancer   *balancer_create(int nroutes);

/* Free the balancer. */
void        balancer_destroy(Balancer *b);

/* Return the next backend string ("host:port") for route index ridx.
 * Thread-safe via atomic fetch_add.  Returns NULL if ridx is out of range
 * or the route has no backends. */
const char *balancer_next(Balancer *b, const Config *cfg, int ridx);
