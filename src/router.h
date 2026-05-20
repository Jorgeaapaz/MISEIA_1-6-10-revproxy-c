#pragma once
#include "config.h"

typedef struct Router Router;

/* Create a router from the loaded configuration.
 * Returns NULL on allocation failure. */
Router *router_create(const Config *cfg);

/* Look up the best matching route index for the given domain.
 * Matching precedence: exact > *.suffix > *
 * Returns index into cfg->routes, or -1 if no match. */
int router_match(const Router *r, const char *domain);

/* Release the router object. */
void router_destroy(Router *r);
