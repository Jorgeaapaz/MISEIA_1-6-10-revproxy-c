#pragma once
#include "utils.h"
#include "event_loop.h"
#include "config.h"
#include "router.h"
#include "balancer.h"
#include <stdint.h>
#include <stddef.h>

typedef enum {
    CONN_READING_HEADER = 0,
    CONN_CONNECTING_BACKEND,
    CONN_TUNNELING,
    CONN_CLOSING,
} ConnState;

typedef struct Conn {
    socket_t    client_fd;
    socket_t    backend_fd;
    ConnState   state;
    char        domain[256];
    char        backend_host[256];
    int         backend_port;
    char        client_ip[46];
    int         client_port;
    int64_t     start_ms;           /* connection start time for latency log */
    int64_t     header_deadline_ms; /* absolute monotonic deadline for header */
    uint8_t     hdr_buf[8192];
    int         hdr_len;
    /* Tunnel buffers: client→backend and backend→client */
    uint8_t     c2b_buf[65536];
    int         c2b_len;
    int         c2b_off;
    uint8_t     b2c_buf[65536];
    int         b2c_len;
    int         b2c_off;
    /* Whether X-Forwarded-For has been injected */
    int         xff_injected;
    int         forwarded_for;     /* copy of config->forwarded_for */
    /* Opaque ctx pointers used when calling evloop_add/mod.
     * Set by the caller (main.c) after conn_create(). */
    void       *client_ctx;
    void       *backend_ctx;
} Conn;

/* Allocate and initialise a new connection context. */
Conn *conn_create(socket_t client_fd, const char *client_ip, int client_port,
                  int read_timeout_ms, int forwarded_for);

/* Release the connection (closes fds if still open). */
void  conn_destroy(Conn *c, EventLoop *el);

/* Drive the per-connection state machine.
 * active_fd  – the fd that fired the event
 * events     – EV_READ | EV_WRITE | EV_ERROR bitmask
 * Returns -1 if the connection should be removed from the event loop and freed.
 */
int   tunnel_pump(Conn *c, socket_t active_fd, uint32_t events,
                  EventLoop *el, Config *cfg, Router *router, Balancer *bal);
