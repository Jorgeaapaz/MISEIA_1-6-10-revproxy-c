#include "router.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ── Router struct ────────────────────────────────────────────────────────── */

struct Router {
    const Config *cfg;
    /* Cache indices for the three match classes */
    /* We store all route indices sorted by type: exact, wildcard-suffix, global */
    int *exact_idx;       /* routes with exact domain (no leading *) */
    int  nexact;
    int *wildcard_idx;    /* routes with *.suffix */
    int  nwildcard;
    int  global_idx;      /* index of route with domain=="*", or -1 */
};

/* ── router_create ────────────────────────────────────────────────────────── */

Router *router_create(const Config *cfg)
{
    if (!cfg) return NULL;

    Router *r = (Router *)calloc(1, sizeof(Router));
    if (!r) return NULL;

    r->cfg        = cfg;
    r->global_idx = -1;

    /* Count route types */
    int nexact    = 0;
    int nwildcard = 0;
    for (int i = 0; i < cfg->nroutes; i++) {
        const char *d = cfg->routes[i].domain;
        if (strcmp(d, "*") == 0)            r->global_idx = i;
        else if (d[0] == '*' && d[1] == '.') nwildcard++;
        else                                 nexact++;
    }

    r->exact_idx    = nexact    ? (int *)malloc(sizeof(int) * (size_t)nexact)    : NULL;
    r->wildcard_idx = nwildcard ? (int *)malloc(sizeof(int) * (size_t)nwildcard) : NULL;

    int ei = 0, wi = 0;
    for (int i = 0; i < cfg->nroutes; i++) {
        const char *d = cfg->routes[i].domain;
        if (strcmp(d, "*") == 0)             { /* already set */ }
        else if (d[0] == '*' && d[1] == '.') { if (r->wildcard_idx) r->wildcard_idx[wi++] = i; }
        else                                 { if (r->exact_idx)    r->exact_idx[ei++]    = i; }
    }

    r->nexact    = ei;
    r->nwildcard = wi;
    return r;
}

/* ── router_match ─────────────────────────────────────────────────────────── */

int router_match(const Router *r, const char *domain)
{
    if (!r || !domain) return -1;

    /* Normalize: lowercase copy, strip port if present */
    char dom[256];
    size_t di = 0;
    for (; domain[di] && di < sizeof(dom) - 1; di++) {
        dom[di] = (char)tolower((unsigned char)domain[di]);
    }
    dom[di] = '\0';

    /* Strip port if present (e.g. "example.com:8080" → "example.com") */
    char *colon = strrchr(dom, ':');
    if (colon) *colon = '\0';

    const Config *cfg = r->cfg;

    /* 1. Exact match */
    for (int i = 0; i < r->nexact; i++) {
        int idx = r->exact_idx[i];
        if (strcmp(cfg->routes[idx].domain, dom) == 0) {
            return idx;
        }
    }

    /* 2. Wildcard subdomain: *.suffix
     * "*.example.com" matches "sub.example.com" but NOT "example.com" */
    for (int i = 0; i < r->nwildcard; i++) {
        int         idx    = r->wildcard_idx[i];
        const char *pat    = cfg->routes[idx].domain; /* "*.suffix" */
        const char *suffix = pat + 1;                 /* ".suffix"  */
        size_t      slen   = strlen(suffix);
        size_t      dlen   = strlen(dom);

        if (dlen <= slen) continue;   /* domain must be longer than suffix */

        /* Check the trailing portion matches and there's at least one char before */
        const char *tail = dom + dlen - slen;
        if (strcmp(tail, suffix) == 0 && tail > dom) {
            return idx;
        }
    }

    /* 3. Global wildcard */
    if (r->global_idx >= 0) {
        return r->global_idx;
    }

    return -1;
}

/* ── router_destroy ───────────────────────────────────────────────────────── */

void router_destroy(Router *r)
{
    if (!r) return;
    free(r->exact_idx);
    free(r->wildcard_idx);
    free(r);
}
