/*
 * Linux epoll backend – implements event_loop.h
 *
 * Uses epoll_create1(EPOLL_CLOEXEC) + edge-triggered EPOLLET mode.
 * One EventLoop per worker thread (each has its own epoll fd).
 */

#ifndef _WIN32

#include "../src/event_loop.h"
#include "../src/utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>

/* ── EventLoop struct ─────────────────────────────────────────────────────── */

struct EventLoop {
    int epoll_fd;
};

/* ── epoll event conversion ───────────────────────────────────────────────── */

static uint32_t to_epoll(uint32_t events)
{
    uint32_t ep = EPOLLET | EPOLLRDHUP;
    if (events & EV_READ)  ep |= EPOLLIN;
    if (events & EV_WRITE) ep |= EPOLLOUT;
    if (events & EV_ERROR) ep |= EPOLLERR | EPOLLHUP;
    return ep;
}

static uint32_t from_epoll(uint32_t ep)
{
    uint32_t ev = 0;
    if (ep & (EPOLLIN  | EPOLLRDHUP)) ev |= EV_READ;
    if (ep & EPOLLOUT)                 ev |= EV_WRITE;
    if (ep & (EPOLLERR | EPOLLHUP))   ev |= EV_ERROR;
    return ev;
}

/* ── evloop_create ────────────────────────────────────────────────────────── */

EventLoop *evloop_create(void)
{
    EventLoop *el = (EventLoop *)calloc(1, sizeof(EventLoop));
    if (!el) return NULL;

    el->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (el->epoll_fd < 0) {
        free(el);
        return NULL;
    }
    return el;
}

/* ── evloop_add ───────────────────────────────────────────────────────────── */

int evloop_add(EventLoop *el, socket_t fd, uint32_t events, void *ctx)
{
    struct epoll_event ee;
    memset(&ee, 0, sizeof(ee));
    ee.events   = to_epoll(events);
    ee.data.ptr = ctx;
    return epoll_ctl(el->epoll_fd, EPOLL_CTL_ADD, fd, &ee) == 0 ? 0 : -1;
}

/* ── evloop_mod ───────────────────────────────────────────────────────────── */

int evloop_mod(EventLoop *el, socket_t fd, uint32_t events, void *ctx)
{
    struct epoll_event ee;
    memset(&ee, 0, sizeof(ee));
    ee.events   = to_epoll(events);
    ee.data.ptr = ctx;
    if (events == 0) {
        /* events==0: temporarily remove from epoll (no notifications).
         * Use EPOLL_CTL_MOD with just EPOLLET to keep fd registered but silent.
         * We cannot pass 0 to epoll_ctl (it requires at least one event bit on
         * some kernels). Use EPOLLERR only to keep alive without spurious wakes. */
        ee.events = EPOLLET | EPOLLERR;
        int r = epoll_ctl(el->epoll_fd, EPOLL_CTL_MOD, fd, &ee);
        if (r != 0 && errno == ENOENT) {
            /* fd not registered yet – ignore */
            return 0;
        }
        return r == 0 ? 0 : -1;
    }
    int r = epoll_ctl(el->epoll_fd, EPOLL_CTL_MOD, fd, &ee);
    if (r != 0 && errno == ENOENT) {
        /* Not registered yet – add it */
        r = epoll_ctl(el->epoll_fd, EPOLL_CTL_ADD, fd, &ee);
    }
    return r == 0 ? 0 : -1;
}

/* ── evloop_del ───────────────────────────────────────────────────────────── */

int evloop_del(EventLoop *el, socket_t fd)
{
    /* From Linux 2.6.9+, the event arg can be NULL for EPOLL_CTL_DEL */
    return epoll_ctl(el->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == 0 ? 0 : -1;
}

/* ── evloop_wait ──────────────────────────────────────────────────────────── */

int evloop_wait(EventLoop *el, EvEvent *out, int maxev, int timeout_ms)
{
    struct epoll_event ep_events[256];
    int cap = maxev < 256 ? maxev : 256;

    int n = epoll_wait(el->epoll_fd, ep_events, cap, timeout_ms);
    if (n < 0) {
        if (errno == EINTR) return 0;   /* interrupted by signal – not an error */
        return -1;
    }

    for (int i = 0; i < n; i++) {
        out[i].ctx    = ep_events[i].data.ptr;
        out[i].events = from_epoll(ep_events[i].events);
    }
    return n;
}

/* ── evloop_destroy ───────────────────────────────────────────────────────── */

void evloop_destroy(EventLoop *el)
{
    if (!el) return;
    if (el->epoll_fd >= 0) close(el->epoll_fd);
    free(el);
}

#endif /* !_WIN32 */
