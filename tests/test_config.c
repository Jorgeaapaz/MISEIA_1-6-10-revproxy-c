/*
 * Unit tests for config.c
 * REQ-T-03: T-03-1 through T-03-6
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <windows.h>
  #include <io.h>
#else
  #include <unistd.h>
#endif

#include "../src/config.h"

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

/* ── Write a temp file and return a path ─────────────────────────────────── */

static char g_tmp_path[512];
static int  g_tmp_idx = 0;

static const char *write_temp(const char *content)
{
#ifdef _WIN32
    snprintf(g_tmp_path, sizeof(g_tmp_path),
             "%s\\proxy_test_%d.toml",
             getenv("TEMP") ? getenv("TEMP") : "C:\\Temp",
             g_tmp_idx++);
#else
    snprintf(g_tmp_path, sizeof(g_tmp_path),
             "/tmp/proxy_test_%d.toml", g_tmp_idx++);
#endif

    FILE *fp = fopen(g_tmp_path, "w");
    if (!fp) return NULL;
    fputs(content, fp);
    fclose(fp);
    return g_tmp_path;
}

static void remove_temp(const char *path)
{
    remove(path);
}

/* ── T-03-1: Minimal valid TOML loads without errors ─────────────────────── */

static void test_valid_minimal(void)
{
    const char *toml =
        "[global]\n"
        "workers = 2\n"
        "connect_timeout_ms = 3000\n"
        "read_timeout_ms = 10000\n"
        "log_level = \"info\"\n"
        "\n"
        "[[listener]]\n"
        "port = 8080\n"
        "tls = false\n"
        "\n"
        "[[route]]\n"
        "domain = \"api.test\"\n"
        "backends = [\"127.0.0.1:9001\", \"127.0.0.1:9002\"]\n";

    const char *path = write_temp(toml);
    TEST_ASSERT_MSG(path != NULL, "temp file creation");

    Config *cfg = (Config *)calloc(1, sizeof(Config));
    TEST_ASSERT_MSG(cfg != NULL, "calloc Config");
    int rc = config_load(path, cfg);
    TEST_ASSERT_MSG(rc == 0, "minimal valid config should load without error");

    if (rc == 0) {
        TEST_ASSERT_MSG(cfg->workers == 2, "workers should be 2");
        TEST_ASSERT_MSG(cfg->connect_timeout_ms == 3000,
                        "connect_timeout_ms should be 3000");
        TEST_ASSERT_MSG(cfg->read_timeout_ms == 10000,
                        "read_timeout_ms should be 10000");
        TEST_ASSERT_MSG(cfg->log_level == LOG_INFO, "log_level should be INFO");
        TEST_ASSERT_MSG(cfg->nlisteners == 1, "should have 1 listener");
        TEST_ASSERT_MSG(cfg->listeners[0].port == 8080, "port should be 8080");
        TEST_ASSERT_MSG(cfg->nroutes == 1, "should have 1 route");
        TEST_ASSERT_MSG(strcmp(cfg->routes[0].domain, "api.test") == 0,
                        "domain should be 'api.test'");
        TEST_ASSERT_MSG(cfg->routes[0].nbackends == 2,
                        "should have 2 backends");
        TEST_ASSERT_MSG(strcmp(cfg->routes[0].backends[0], "127.0.0.1:9001") == 0,
                        "first backend should be 127.0.0.1:9001");
    }

    free(cfg);
    remove_temp(path);
}

/* ── T-03-2: Missing [[listener]] → error ────────────────────────────────── */

static void test_missing_listener(void)
{
    const char *toml =
        "[global]\n"
        "workers = 1\n"
        "\n"
        "[[route]]\n"
        "domain = \"api.test\"\n"
        "backends = [\"127.0.0.1:9001\"]\n";

    const char *path = write_temp(toml);
    TEST_ASSERT_MSG(path != NULL, "temp file creation");

    Config *cfg = (Config *)calloc(1, sizeof(Config));
    TEST_ASSERT_MSG(cfg != NULL, "calloc Config");
    int rc = config_load(path, cfg);
    TEST_ASSERT_MSG(rc != 0, "missing listener should cause an error");

    free(cfg);
    remove_temp(path);
}

/* ── T-03-3: Missing [[route]] → error ───────────────────────────────────── */

static void test_missing_route(void)
{
    const char *toml =
        "[[listener]]\n"
        "port = 8080\n"
        "tls = false\n";

    const char *path = write_temp(toml);
    TEST_ASSERT_MSG(path != NULL, "temp file creation");

    Config *cfg = (Config *)calloc(1, sizeof(Config));
    TEST_ASSERT_MSG(cfg != NULL, "calloc Config");
    int rc = config_load(path, cfg);
    TEST_ASSERT_MSG(rc != 0, "missing route should cause an error");

    free(cfg);
    remove_temp(path);
}

/* ── T-03-4: Port out of range → error ───────────────────────────────────── */

static void test_invalid_port(void)
{
    const char *toml =
        "[[listener]]\n"
        "port = 0\n"       /* port 0 is invalid per spec */
        "tls = false\n"
        "\n"
        "[[route]]\n"
        "domain = \"api.test\"\n"
        "backends = [\"127.0.0.1:9001\"]\n";

    const char *path = write_temp(toml);
    TEST_ASSERT_MSG(path != NULL, "temp file creation");

    Config *cfg = (Config *)calloc(1, sizeof(Config));
    TEST_ASSERT_MSG(cfg != NULL, "calloc Config");
    int rc = config_load(path, cfg);
    TEST_ASSERT_MSG(rc != 0, "port=0 should be rejected");

    free(cfg);
    remove_temp(path);
}

/* ── T-03-5: Backend with invalid format → error ─────────────────────────── */

static void test_invalid_backend(void)
{
    const char *toml =
        "[[listener]]\n"
        "port = 8080\n"
        "tls = false\n"
        "\n"
        "[[route]]\n"
        "domain = \"api.test\"\n"
        "backends = [\"not-a-valid-backend\"]\n";  /* missing port */

    const char *path = write_temp(toml);
    TEST_ASSERT_MSG(path != NULL, "temp file creation");

    Config *cfg = (Config *)calloc(1, sizeof(Config));
    TEST_ASSERT_MSG(cfg != NULL, "calloc Config");
    int rc = config_load(path, cfg);
    TEST_ASSERT_MSG(rc != 0, "invalid backend format should be rejected");

    free(cfg);
    remove_temp(path);
}

/* ── T-03-6: [global] defaults when section is omitted ───────────────────── */

static void test_defaults(void)
{
    const char *toml =
        "[[listener]]\n"
        "port = 9090\n"
        "tls = false\n"
        "\n"
        "[[route]]\n"
        "domain = \"test.local\"\n"
        "backends = [\"127.0.0.1:8000\"]\n";

    const char *path = write_temp(toml);
    TEST_ASSERT_MSG(path != NULL, "temp file creation");

    Config *cfg = (Config *)calloc(1, sizeof(Config));
    TEST_ASSERT_MSG(cfg != NULL, "calloc Config");
    int rc = config_load(path, cfg);
    TEST_ASSERT_MSG(rc == 0, "config without [global] should load with defaults");

    if (rc == 0) {
        TEST_ASSERT_MSG(cfg->connect_timeout_ms == 5000,
                        "default connect_timeout_ms should be 5000");
        TEST_ASSERT_MSG(cfg->read_timeout_ms == 30000,
                        "default read_timeout_ms should be 30000");
        TEST_ASSERT_MSG(cfg->log_level == LOG_INFO,
                        "default log_level should be INFO");
        TEST_ASSERT_MSG(cfg->workers == 0,
                        "default workers should be 0 (= num CPUs)");
    }

    free(cfg);
    remove_temp(path);
}

/* ── T-03-7: Multiple routes, all loaded ────────────────────────────────── */

static void test_multiple_routes(void)
{
    const char *toml =
        "[[listener]]\n"
        "port = 8080\n"
        "tls = false\n"
        "\n"
        "[[route]]\n"
        "domain = \"api.test\"\n"
        "backends = [\"127.0.0.1:9001\", \"127.0.0.1:9002\"]\n"
        "\n"
        "[[route]]\n"
        "domain = \"web.test\"\n"
        "backends = [\"127.0.0.1:9003\"]\n"
        "\n"
        "[[route]]\n"
        "domain = \"*\"\n"
        "backends = [\"127.0.0.1:9000\"]\n";

    const char *path = write_temp(toml);
    TEST_ASSERT_MSG(path != NULL, "temp file creation");

    Config *cfg = (Config *)calloc(1, sizeof(Config));
    TEST_ASSERT_MSG(cfg != NULL, "calloc Config");
    int rc = config_load(path, cfg);
    TEST_ASSERT_MSG(rc == 0, "multiple routes config should load");

    if (rc == 0) {
        TEST_ASSERT_MSG(cfg->nroutes == 3, "should have 3 routes");
        TEST_ASSERT_MSG(strcmp(cfg->routes[0].domain, "api.test") == 0,
                        "first route domain");
        TEST_ASSERT_MSG(cfg->routes[0].nbackends == 2,
                        "first route has 2 backends");
        TEST_ASSERT_MSG(strcmp(cfg->routes[1].domain, "web.test") == 0,
                        "second route domain");
        TEST_ASSERT_MSG(strcmp(cfg->routes[2].domain, "*") == 0,
                        "third route is global wildcard");
    }

    free(cfg);
    remove_temp(path);
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== test_config ===\n");

    test_valid_minimal();
    test_missing_listener();
    test_missing_route();
    test_invalid_port();
    test_invalid_backend();
    test_defaults();
    test_multiple_routes();

    printf("Tests: %d run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed ? 1 : 0;
}
