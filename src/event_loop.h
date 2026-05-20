#pragma once
#include "utils.h"
#include <stdint.h>

/* Event flags */
#define EV_READ   0x01u
#define EV_WRITE  0x02u
#define EV_ERROR  0x04u

/* A single ready event returned by evloop_wait() */
typedef struct {
    void    *ctx;      /* user context pointer registered with evloop_add */
    uint32_t events;   /* bitmask of EV_READ | EV_WRITE | EV_ERROR */
} EvEvent;

/* Opaque event loop handle (implemented in platform/epoll.c or platform/iocp.c) */
typedef struct EventLoop EventLoop;

/* Create a new event loop for the calling thread.
 * Returns NULL on error. */
EventLoop *evloop_create(void);

/* Register fd with the given event mask and user context.
 * Returns 0 on success, -1 on error. */
int evloop_add(EventLoop *el, socket_t fd, uint32_t events, void *ctx);

/* Modify registered fd's event mask / context.
 * Returns 0 on success, -1 on error. */
int evloop_mod(EventLoop *el, socket_t fd, uint32_t events, void *ctx);

/* Deregister fd.  Returns 0 on success, -1 on error. */
int evloop_del(EventLoop *el, socket_t fd);

/* Wait for events.
 * out     – caller-allocated array of EvEvent of size maxev
 * timeout_ms – -1 = block indefinitely, 0 = non-blocking poll
 * Returns number of events (0 = timeout), -1 on error. */
int evloop_wait(EventLoop *el, EvEvent *out, int maxev, int timeout_ms);

/* Destroy the event loop and free resources. */
void evloop_destroy(EventLoop *el);
