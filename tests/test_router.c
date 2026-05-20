/*
 * Unit tests for router.c
 * REQ-T-01: T-01-1 through T-01-6
 *
 * Uses a minimal Unity-compatible test runner (or Unity if available).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../src/config.h"
#include "../src/router.h"

/* ── Minimal test harness ─────────────────────────────────────────────────── */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond) do { \
    g_tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond); \
        g_tests_failed++; \
    } \
} while (0)

#define TEST_ASSERT_MSG(cond, msg) do { \
    g_tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d] %s: %s\n", __FILE__, __LINE__, #cond, msg); \
        g_tests_failed++; \
    } \
} while (0)

/* ── Build a Config with known routes ────────────────────────────────────── */

static Config *make_config(void)
{
    Config *cfg = (Config *)calloc(1, sizeof(Config));
    if (!cfg) return NULL;

    cfg->workers            = 1;
    cfg->connect_timeout_ms = 5000;
    cfg->read_timeout_ms    = 30000;
    cfg->log_level          = LOG_INFO;

    /* Listener */
    cfg->nlisteners = 1;
    cfg->listeners[0].port = 8080;
    cfg->listeners[0].tls  = 0;

    /* Route 0: exact match */
    snprintf(cfg->routes[0].domain, sizeof(cfg->routes[0].domain),
             "api.example.com");
    snprintf(cfg->routes[0].backends[0], 256, "127.0.0.1:9001");
    cfg->routes[0].nbackends = 1;

    /* Route 1: wildcard subdomain */
    snprintf(cfg->routes[1].domain, sizeof(cfg->routes[1].domain),
             "*.example.com");
    snprintf(cfg->routes[1].backends[0], 256, "127.0.0.1:9002");
    cfg->routes[1].nbackends = 1;

    /* Route 2: another exact domain */
    snprintf(cfg->routes[2].domain, sizeof(cfg->routes[2].domain),
             "web.test");
    snprintf(cfg->routes[2].backends[0], 256, "127.0.0.1:9003");
    cfg->routes[2].nbackends = 1;

    /* Route 3: global wildcard */
    snprintf(cfg->routes[3].domain, sizeof(cfg->routes[3].domain), "*");
    snprintf(cfg->routes[3].backends[0], 256, "127.0.0.1:9000");
    cfg->routes[3].nbackends = 1;

    cfg->nroutes = 4;
    return cfg;
}

/* ── T-01-1: Exact match ──────────────────────────────────────────────────── */

static void test_exact_match(void)
{
    Config *cfg = make_config();
    Router *r   = router_create(cfg);

    int idx = router_match(r, "api.example.com");
    TEST_ASSERT_MSG(idx == 0, "exact match should return route index 0");

    router_destroy(r);
    free(cfg);
}

/* ── T-01-2: Wildcard subdomain match ────────────────────────────────────── */

static void test_wildcard_subdomain_match(void)
{
    Config *cfg = make_config();
    Router *r   = router_create(cfg);

    int idx = router_match(r, "sub.example.com");
    TEST_ASSERT_MSG(idx == 1, "*.example.com should match sub.example.com");

    idx = router_match(r, "foo.example.com");
    TEST_ASSERT_MSG(idx == 1, "*.example.com should match foo.example.com");

    router_destroy(r);
    free(cfg);
}

/* ── T-01-3: Wildcard subdomain does NOT match apex ─────────────────────── */

static void test_wildcard_no_apex_match(void)
{
    Config *cfg = make_config();
    Router *r   = router_create(cfg);

    /* "example.com" should NOT match "*.example.com" (no subdomain part).
     * It falls through to global wildcard "*" (route 3). */
    int idx = router_match(r, "example.com");
    TEST_ASSERT_MSG(idx == 3,
        "*.example.com should NOT match example.com; global wildcard should");

    router_destroy(r);
    free(cfg);
}

/* ── T-01-4: Global wildcard captures unmapped domain ────────────────────── */

static void test_global_wildcard(void)
{
    Config *cfg = make_config();
    Router *r   = router_create(cfg);

    int idx = router_match(r, "unknown.domain.xyz");
    TEST_ASSERT_MSG(idx == 3, "global wildcard should capture unknown domains");

    router_destroy(r);
    free(cfg);
}

/* ── T-01-5: No match when no global wildcard ────────────────────────────── */

static void test_no_match(void)
{
    /* Build a config WITHOUT a global wildcard */
    Config *cfg = (Config *)calloc(1, sizeof(Config));
    cfg->nlisteners = 1;
    cfg->listeners[0].port = 8080;

    snprintf(cfg->routes[0].domain, sizeof(cfg->routes[0].domain),
             "api.example.com");
    snprintf(cfg->routes[0].backends[0], 256, "127.0.0.1:9001");
    cfg->routes[0].nbackends = 1;
    cfg->nroutes = 1;

    Router *r = router_create(cfg);

    int idx = router_match(r, "other.example.com");
    TEST_ASSERT_MSG(idx == -1,
        "no match should return -1 when no global wildcard exists");

    router_destroy(r);
    free(cfg);
}

/* ── T-01-6: Exact match takes precedence over wildcard ───────────────────── */

static void test_exact_over_wildcard(void)
{
    Config *cfg = make_config();
    Router *r   = router_create(cfg);

    /* "api.example.com" is route 0 (exact); "*.example.com" is route 1.
     * The exact match must win. */
    int idx = router_match(r, "api.example.com");
    TEST_ASSERT_MSG(idx == 0,
        "exact match must have precedence over wildcard subdomain");

    router_destroy(r);
    free(cfg);
}

/* ── Additional: port stripping ───────────────────────────────────────────── */

static void test_port_stripping(void)
{
    Config *cfg = make_config();
    Router *r   = router_create(cfg);

    /* Host header may include port: "api.example.com:8080" */
    int idx = router_match(r, "api.example.com:8080");
    TEST_ASSERT_MSG(idx == 0,
        "router should strip port from domain before matching");

    router_destroy(r);
    free(cfg);
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== test_router ===\n");

    test_exact_match();
    test_wildcard_subdomain_match();
    test_wildcard_no_apex_match();
    test_global_wildcard();
    test_no_match();
    test_exact_over_wildcard();
    test_port_stripping();

    printf("Tests: %d run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed ? 1 : 0;
}
