#include "balancer.h"
#include <stdlib.h>
#include <string.h>

/* ── balancer_create ──────────────────────────────────────────────────────── */

Balancer *balancer_create(int nroutes)
{
    (void)nroutes;   /* We always allocate MAX_ROUTES counters */
    Balancer *b = (Balancer *)calloc(1, sizeof(Balancer));
    if (!b) return NULL;
    for (int i = 0; i < MAX_ROUTES; i++) {
        atomic_init(&b->counters[i].idx, 0);
    }
    return b;
}

/* ── balancer_destroy ─────────────────────────────────────────────────────── */

void balancer_destroy(Balancer *b)
{
    free(b);
}

/* ── balancer_next ────────────────────────────────────────────────────────── */

const char *balancer_next(Balancer *b, const Config *cfg, int ridx)
{
    if (!b || !cfg) return NULL;
    if (ridx < 0 || ridx >= cfg->nroutes) return NULL;

    const RouteCfg *route = &cfg->routes[ridx];
    if (route->nbackends <= 0) return NULL;

    /* Atomically increment and wrap */
    int raw = atomic_fetch_add_explicit(&b->counters[ridx].idx, 1,
                                        memory_order_relaxed);
    /* Handle potential negative wrap-around from overflow */
    if (raw < 0) raw = -raw;
    int pick = raw % route->nbackends;
    return route->backends[pick];
}
