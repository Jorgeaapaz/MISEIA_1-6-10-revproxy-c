#include "utils.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <time.h>
  #include <fcntl.h>
#endif

/* ── time_ms ─────────────────────────────────────────────────────────────── */

int64_t time_ms(void)
{
#ifdef _WIN32
    /* GetTickCount64 gives ms since system start, always monotonic */
    return (int64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
#endif
}

/* ── sock_set_nonblocking ─────────────────────────────────────────────────── */

int sock_set_nonblocking(socket_t fd)
{
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1 ? -1 : 0;
#endif
}

/* ── parse_hostport ───────────────────────────────────────────────────────── */

int parse_hostport(const char *s, char *host_out, size_t hlen, int *port_out)
{
    if (!s || !host_out || !port_out || hlen == 0) return -1;

    /* Find last colon to support IPv6 addresses */
    const char *colon = strrchr(s, ':');
    if (!colon) return -1;

    size_t host_len = (size_t)(colon - s);
    if (host_len == 0 || host_len >= hlen) return -1;

    memcpy(host_out, s, host_len);
    host_out[host_len] = '\0';

    char *end = NULL;
    long port = strtol(colon + 1, &end, 10);
    if (end == colon + 1 || *end != '\0') return -1;
    if (port < 1 || port > 65535) return -1;

    *port_out = (int)port;
    return 0;
}

/* ── strlcpy_safe ─────────────────────────────────────────────────────────── */

size_t strlcpy_safe(char *dst, const char *src, size_t size)
{
    if (size == 0) return 0;
    size_t i;
    for (i = 0; i < size - 1 && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
    return i;
}

/* ── str_tolower ──────────────────────────────────────────────────────────── */

void str_tolower(char *s)
{
    if (!s) return;
    for (; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
    }
}

/* ── str_rtrim ────────────────────────────────────────────────────────────── */

void str_rtrim(char *s)
{
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                       s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
}
