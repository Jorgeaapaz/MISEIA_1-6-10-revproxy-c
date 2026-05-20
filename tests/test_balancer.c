/*
 * Unit tests for balancer.c
 * REQ-T-02: T-02-1 through T-02-3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/config.h"
#include "../src/balancer.h"

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <process.h>
#else
  #include <pthread.h>
#endif

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

/* ── Build a Config with a route that has N backends ─────────────────────── */

static Config *make_config_n(int nbackends)
{
    Config *cfg = (Config *)calloc(1, sizeof(Config));
    cfg->nlisteners = 1;
    cfg->listeners[0].port = 8080;
    cfg->nroutes = 1;

    snprintf(cfg->routes[0].domain, sizeof(cfg->routes[0].domain), "test.local");
    cfg->routes[0].nbackends = nbackends;

    for (int i = 0; i < nbackends; i++) {
        snprintf(cfg->routes[0].backends[i], 256,
                 "127.0.0.1:%d", 9000 + i);
    }
    return cfg;
}

/* ── T-02-1: Round-robin over 3 backends cycles 0→1→2→0 ─────────────────── */

static void test_round_robin_3(void)
{
    Config   *cfg = make_config_n(3);
    Balancer *b   = balancer_create(cfg->nroutes);

    const char *b0 = balancer_next(b, cfg, 0);
    const char *b1 = balancer_next(b, cfg, 0);
    const char *b2 = balancer_next(b, cfg, 0);
    const char *b3 = balancer_next(b, cfg, 0);  /* should wrap to 0 */

    TEST_ASSERT_MSG(b0 != NULL, "first call should return non-NULL");
    TEST_ASSERT_MSG(b1 != NULL, "second call should return non-NULL");
    TEST_ASSERT_MSG(b2 != NULL, "third call should return non-NULL");
    TEST_ASSERT_MSG(b3 != NULL, "fourth call should return non-NULL");

    /* They should cycle through all 3 backends */
    TEST_ASSERT_MSG(strcmp(b0, cfg->routes[0].backends[0]) == 0 ||
                    strcmp(b0, cfg->routes[0].backends[1]) == 0 ||
                    strcmp(b0, cfg->routes[0].backends[2]) == 0,
                    "first backend should be one of the three");

    /* The fourth call must equal the first (wraparound) */
    TEST_ASSERT_MSG(strcmp(b3, b0) == 0,
                    "fourth call must wrap around to first backend");

    /* All three distinct */
    TEST_ASSERT_MSG(strcmp(b0, b1) != 0, "backends 0 and 1 must differ");
    TEST_ASSERT_MSG(strcmp(b1, b2) != 0, "backends 1 and 2 must differ");
    TEST_ASSERT_MSG(strcmp(b0, b2) != 0, "backends 0 and 2 must differ");

    balancer_destroy(b);
    free(cfg);
}

/* ── T-02-2: Single backend always returns same value ────────────────────── */

static void test_single_backend(void)
{
    Config   *cfg = make_config_n(1);
    Balancer *b   = balancer_create(cfg->nroutes);

    const char *first = balancer_next(b, cfg, 0);
    TEST_ASSERT_MSG(first != NULL, "single backend should return non-NULL");

    for (int i = 0; i < 10; i++) {
        const char *s = balancer_next(b, cfg, 0);
        TEST_ASSERT_MSG(s != NULL && strcmp(s, first) == 0,
                        "single backend should always return the same value");
    }

    balancer_destroy(b);
    free(cfg);
}

/* ── T-02-3: Thread-safety – 4 threads each call 250 times ──────────────── */

typedef struct {
    Balancer *b;
    Config   *cfg;
    int       calls;
    int       counts[MAX_BACKENDS];   /* how many times each backend was selected */
} ThreadArg;

#ifdef _WIN32
static unsigned __stdcall rr_thread(void *arg)
#else
static void *rr_thread(void *arg)
#endif
{
    ThreadArg *ta = (ThreadArg *)arg;
    for (int i = 0; i < ta->calls; i++) {
        const char *s = balancer_next(ta->b, ta->cfg, 0);
        /* Find index in backends array */
        for (int j = 0; j < ta->cfg->routes[0].nbackends; j++) {
            if (s && strcmp(s, ta->cfg->routes[0].backends[j]) == 0) {
                ta->counts[j]++;
                break;
            }
        }
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void test_thread_safety(void)
{
    const int NTHREADS = 4;
    const int CALLS    = 250;

    Config   *cfg = make_config_n(2);
    Balancer *b   = balancer_create(cfg->nroutes);

    ThreadArg args[4];
    memset(args, 0, sizeof(args));

#ifdef _WIN32
    HANDLE threads[4];
#else
    pthread_t threads[4];
#endif

    for (int i = 0; i < NTHREADS; i++) {
        args[i].b    = b;
        args[i].cfg  = cfg;
        args[i].calls = CALLS;
#ifdef _WIN32
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, rr_thread, &args[i], 0, NULL);
#else
        pthread_create(&threads[i], NULL, rr_thread, &args[i]);
#endif
    }

    for (int i = 0; i < NTHREADS; i++) {
#ifdef _WIN32
        WaitForSingleObject(threads[i], INFINITE);
        CloseHandle(threads[i]);
#else
        pthread_join(threads[i], NULL);
#endif
    }

    /* Total calls = NTHREADS * CALLS = 1000.
     * With 2 backends and perfect round-robin, each should get 500.
     * Due to threads starting at different times, allow ±10 tolerance. */
    int total = 0;
    for (int i = 0; i < NTHREADS; i++) {
        total += args[i].counts[0] + args[i].counts[1];
    }

    int count0 = 0, count1 = 0;
    for (int i = 0; i < NTHREADS; i++) {
        count0 += args[i].counts[0];
        count1 += args[i].counts[1];
    }

    TEST_ASSERT_MSG(total == NTHREADS * CALLS,
                    "total dispatches must equal total calls");
    TEST_ASSERT_MSG(count0 > 0, "backend 0 must receive some requests");
    TEST_ASSERT_MSG(count1 > 0, "backend 1 must receive some requests");

    /* With round-robin, each backend should get ~50%. Allow 5% slack. */
    int expected = NTHREADS * CALLS / 2;
    int slack    = expected / 10 + 5;
    TEST_ASSERT_MSG(abs(count0 - expected) <= slack,
                    "backend 0 should receive approximately 50% of requests");

    balancer_destroy(b);
    free(cfg);
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== test_balancer ===\n");

    test_round_robin_3();
    test_single_backend();
    test_thread_safety();

    printf("Tests: %d run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed ? 1 : 0;
}
