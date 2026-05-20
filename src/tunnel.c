#include "tunnel.h"
#include "dispatcher.h"
#include "log.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #define WOULD_BLOCK(e) ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
  #define EINPROGRESS_VAL WSAEINPROGRESS
  #define EWOULDBLOCK_VAL WSAEWOULDBLOCK
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #define WOULD_BLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK || (e) == EINPROGRESS)
  #define EINPROGRESS_VAL EINPROGRESS
  #define EWOULDBLOCK_VAL EAGAIN
#endif
/* sock_errno() is defined in utils.h (included above) */

/* ── Static response helpers ──────────────────────────────────────────────── */

static void send_error(socket_t fd, int code, const char *reason)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "HTTP/1.1 %d %s\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n",
        code, reason);
    if (n > 0) {
        (void)send(fd, buf, (size_t)n, 0);
    }
}

/* ── inject_xff ───────────────────────────────────────────────────────────── */

/* Inject X-Forwarded-For header into an HTTP request buffer.
 * buf holds hdr_len bytes. We insert the header right after the first line.
 * Returns new length, or original length if injection fails. */
static int inject_xff(uint8_t *buf, int buf_len, int buf_cap,
                      const char *client_ip)
{
    /* Find end of request line (\r\n or \n) */
    int pos = 0;
    while (pos < buf_len && buf[pos] != '\n') pos++;
    if (pos >= buf_len) return buf_len;
    pos++;   /* skip \n */

    char xff[128];
    int xlen = snprintf(xff, sizeof(xff),
                        "X-Forwarded-For: %s\r\n", client_ip);
    if (xlen <= 0 || xlen >= (int)sizeof(xff)) return buf_len;

    /* Check capacity */
    if (buf_len + xlen > buf_cap) return buf_len;

    /* Shift existing data to make room */
    memmove(buf + pos + xlen, buf + pos, (size_t)(buf_len - pos));
    memcpy(buf + pos, xff, (size_t)xlen);
    return buf_len + xlen;
}

/* ── conn_create ──────────────────────────────────────────────────────────── */

Conn *conn_create(socket_t client_fd, const char *client_ip, int client_port,
                  int read_timeout_ms, int forwarded_for)
{
    Conn *c = (Conn *)calloc(1, sizeof(Conn));
    if (!c) return NULL;

    c->client_fd  = client_fd;
    c->backend_fd = SOCK_INVALID;
    c->state      = CONN_READING_HEADER;
    c->start_ms   = time_ms();
    c->header_deadline_ms = c->start_ms + (int64_t)read_timeout_ms;
    c->forwarded_for = forwarded_for;
    c->client_ctx = NULL;   /* set by caller after conn_create() */
    c->backend_ctx = NULL;  /* set by caller when backend is connected */

    strlcpy_safe(c->client_ip, client_ip, sizeof(c->client_ip));
    c->client_port = client_port;

    return c;
}

/* ── conn_destroy ─────────────────────────────────────────────────────────── */

void conn_destroy(Conn *c, EventLoop *el)
{
    if (!c) return;
    if (c->client_fd != SOCK_INVALID) {
        if (el) evloop_del(el, c->client_fd);
        sock_close(c->client_fd);
        c->client_fd = SOCK_INVALID;
    }
    if (c->backend_fd != SOCK_INVALID) {
        if (el) evloop_del(el, c->backend_fd);
        sock_close(c->backend_fd);
        c->backend_fd = SOCK_INVALID;
    }
    /* Free opaque ctx structs allocated by the caller (main.c FdCtx*) */
    free(c->client_ctx);
    c->client_ctx = NULL;
    free(c->backend_ctx);
    c->backend_ctx = NULL;
    free(c);
}

/* ── connect_to_backend ───────────────────────────────────────────────────── */

static socket_t connect_to_backend(const char *host, int port,
                                    int connect_timeout_ms)
{
    (void)connect_timeout_ms;   /* enforced via event loop timeout */

    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == SOCK_INVALID) return SOCK_INVALID;

    /* Non-blocking */
    if (sock_set_nonblocking(fd) != 0) {
        sock_close(fd);
        return SOCK_INVALID;
    }

    /* Disable Nagle */
#ifndef _WIN32
    {
        int yes = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    }
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);

    /* Resolve host (numeric only for now) */
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        sock_close(fd);
        return SOCK_INVALID;
    }

    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc == 0) {
        /* Connected immediately */
        return fd;
    }

    int err = sock_errno();
    if (WOULD_BLOCK(err)) {
        /* Connection in progress – caller will wait for EV_WRITE */
        return fd;
    }

    sock_close(fd);
    return SOCK_INVALID;
}

/* ── do_read ──────────────────────────────────────────────────────────────── */

/* Read from src into buf[off + *len .. cap].
 * Returns bytes read, 0 on peer close, -1 on would-block, -2 on error. */
static int do_read(socket_t src, uint8_t *buf, int *len, int cap)
{
    int space = cap - *len;
    if (space <= 0) return -1;

#ifdef _WIN32
    int n = recv(src, (char *)(buf + *len), space, 0);
#else
    ssize_t n = recv(src, buf + *len, (size_t)space, 0);
#endif

    if (n > 0) {
        *len += (int)n;
        return (int)n;
    }
    if (n == 0) return 0;   /* EOF */

    int e = sock_errno();
    if (WOULD_BLOCK(e)) return -1;
    return -2;   /* hard error */
}

/* ── do_flush ─────────────────────────────────────────────────────────────── */

/* Flush buf[off .. off+len] to dst.
 * Updates *off and *len in place.
 * Returns 1 if all data sent, 0 if partial (EWOULDBLOCK), -1 on error. */
static int do_flush(socket_t dst, uint8_t *buf, int *off, int *len)
{
    while (*len > 0) {
#ifdef _WIN32
        int n = send(dst, (const char *)(buf + *off), *len, 0);
#else
        ssize_t n = send(dst, buf + *off, (size_t)*len, 0);
#endif
        if (n > 0) {
            *off += (int)n;
            *len -= (int)n;
            if (*len == 0) { *off = 0; return 1; }
        } else {
            int e = sock_errno();
            if (WOULD_BLOCK(e)) return 0;
            return -1;
        }
    }
    *off = 0;
    return 1;
}

/* ── tunnel_pump ──────────────────────────────────────────────────────────── */

int tunnel_pump(Conn *c, socket_t active_fd, uint32_t events,
                EventLoop *el, Config *cfg, Router *router, Balancer *bal)
{
    /* Check for errors on any fd */
    if (events & EV_ERROR) {
        return -1;
    }

    switch (c->state) {

    /* ── READING HEADER ───────────────────────────────────────────────────── */
    case CONN_READING_HEADER: {
        /* Timeout check */
        if (time_ms() > c->header_deadline_ms) {
            send_error(c->client_fd, 408, "Request Timeout");
            return -1;
        }

        if (events & EV_READ) {
            /* Check header buffer overflow */
            if (c->hdr_len >= (int)sizeof(c->hdr_buf)) {
                send_error(c->client_fd, 431, "Request Header Fields Too Large");
                return -1;
            }

            int space = (int)sizeof(c->hdr_buf) - c->hdr_len;
#ifdef _WIN32
            int n = recv(c->client_fd,
                         (char *)c->hdr_buf + c->hdr_len, space, 0);
#else
            ssize_t n = recv(c->client_fd,
                             c->hdr_buf + c->hdr_len, (size_t)space, 0);
#endif
            if (n > 0) {
                c->hdr_len += (int)n;
            } else if (n == 0) {
                return -1;   /* client closed early */
            } else {
                int e = sock_errno();
                if (!WOULD_BLOCK(e)) return -1;
            }

            /* Try to extract Host */
            char domain[256] = {0};
            int rc = dispatcher_feed(c->hdr_buf, c->hdr_len,
                                     domain, sizeof(domain));
            if (rc == 1) {
                /* Host found – look up route */
                strlcpy_safe(c->domain, domain, sizeof(c->domain));

                int ridx = router_match(router, domain);
                if (ridx < 0) {
                    send_error(c->client_fd, 502, "Bad Gateway");
                    return -1;
                }

                const char *backend_str = balancer_next(bal, cfg, ridx);
                if (!backend_str) {
                    send_error(c->client_fd, 502, "Bad Gateway");
                    return -1;
                }

                if (parse_hostport(backend_str, c->backend_host,
                                   sizeof(c->backend_host),
                                   &c->backend_port) != 0) {
                    send_error(c->client_fd, 502, "Bad Gateway");
                    return -1;
                }

                /* Optionally inject X-Forwarded-For */
                if (c->forwarded_for && !c->xff_injected) {
                    c->hdr_len = inject_xff(c->hdr_buf, c->hdr_len,
                                            (int)sizeof(c->hdr_buf),
                                            c->client_ip);
                    c->xff_injected = 1;
                }

                /* Initiate backend connection */
                socket_t bfd = connect_to_backend(c->backend_host,
                                                   c->backend_port,
                                                   cfg->connect_timeout_ms);
                if (bfd == SOCK_INVALID) {
                    send_error(c->client_fd, 502, "Bad Gateway");
                    return -1;
                }

                c->backend_fd = bfd;

                /* Copy header into c2b_buf */
                int copy = c->hdr_len < (int)sizeof(c->c2b_buf)
                           ? c->hdr_len : (int)sizeof(c->c2b_buf);
                memcpy(c->c2b_buf, c->hdr_buf, (size_t)copy);
                c->c2b_len = copy;
                c->c2b_off = 0;

                c->state = CONN_CONNECTING_BACKEND;

                /* Watch backend fd for writability (connect completion) */
                /* backend_fd will be registered by caller after we return,
                 * using c->backend_ctx.  For now just stop watching client. */
                evloop_mod(el, c->client_fd, 0, c->client_ctx);

            } else if (rc == -1) {
                /* Header too large or parse error */
                if (c->hdr_len >= (int)sizeof(c->hdr_buf)) {
                    send_error(c->client_fd, 431, "Request Header Fields Too Large");
                } else {
                    send_error(c->client_fd, 400, "Bad Request");
                }
                return -1;
            }
            /* rc == 0: need more data – stay in READING_HEADER */
        }
        break;
    }

    /* ── CONNECTING BACKEND ───────────────────────────────────────────────── */
    case CONN_CONNECTING_BACKEND: {
        if (active_fd == c->backend_fd) {
            if (events & EV_ERROR) {
                send_error(c->client_fd, 504, "Gateway Timeout");
                return -1;
            }
            if (events & EV_WRITE) {
                /* Check if connect succeeded */
                int err = 0;
#ifdef _WIN32
                int errlen = sizeof(err);
                getsockopt(c->backend_fd, SOL_SOCKET, SO_ERROR,
                           (char *)&err, &errlen);
#else
                socklen_t errlen = sizeof(err);
                getsockopt(c->backend_fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
#endif
                if (err != 0) {
                    send_error(c->client_fd, 502, "Bad Gateway");
                    return -1;
                }

                LOG_INFO("%s:%d -> %s -> %s:%d (%lld ms)",
                         c->client_ip, c->client_port,
                         c->domain,
                         c->backend_host, c->backend_port,
                         (long long)(time_ms() - c->start_ms));

                c->state = CONN_TUNNELING;

                /* Re-register both fds for bidirectional tunneling */
                evloop_mod(el, c->client_fd,  EV_READ | EV_WRITE, c->client_ctx);
                evloop_mod(el, c->backend_fd, EV_READ | EV_WRITE, c->backend_ctx);
            }
        }
        break;
    }

    /* ── TUNNELING ────────────────────────────────────────────────────────── */
    case CONN_TUNNELING: {
        int done = 0;

        /* Read from client into c2b buffer if space available */
        if ((events & EV_READ) && active_fd == c->client_fd) {
            if (c->c2b_len < (int)sizeof(c->c2b_buf)) {
                int r = do_read(c->client_fd, c->c2b_buf, &c->c2b_len,
                                (int)sizeof(c->c2b_buf));
                if (r == 0) done = 1;   /* client EOF */
                else if (r == -2) return -1;
            }
        }

        /* Read from backend into b2c buffer if space available */
        if ((events & EV_READ) && active_fd == c->backend_fd) {
            if (c->b2c_len < (int)sizeof(c->b2c_buf)) {
                int r = do_read(c->backend_fd, c->b2c_buf, &c->b2c_len,
                                (int)sizeof(c->b2c_buf));
                if (r == 0) done = 1;   /* backend EOF */
                else if (r == -2) return -1;
            }
        }

        /* Flush c2b to backend.
         * Attempt whenever there is buffered data, not only on EV_WRITE.
         * On Windows, FD_WRITE only fires after a previous WSAEWOULDBLOCK;
         * if the socket is immediately writable we must push proactively. */
        if (c->c2b_len > 0) {
            int r = do_flush(c->backend_fd, c->c2b_buf,
                             &c->c2b_off, &c->c2b_len);
            if (r == -1) return -1;
        }

        /* Flush b2c to client – same reasoning. */
        if (c->b2c_len > 0) {
            int r = do_flush(c->client_fd, c->b2c_buf,
                             &c->b2c_off, &c->b2c_len);
            if (r == -1) return -1;
        }

        if (done) {
            /* Drain remaining b2c data to client */
            if (c->b2c_len > 0) {
                do_flush(c->client_fd, c->b2c_buf, &c->b2c_off, &c->b2c_len);
            }
            return -1;
        }

        /* Update interest sets */
        uint32_t client_ev  = EV_READ | EV_ERROR;
        uint32_t backend_ev = EV_READ | EV_ERROR;

        if (c->c2b_len > 0) backend_ev |= EV_WRITE;
        if (c->b2c_len > 0) client_ev  |= EV_WRITE;

        evloop_mod(el, c->client_fd,  client_ev,  c->client_ctx);
        evloop_mod(el, c->backend_fd, backend_ev, c->backend_ctx);
        break;
    }

    case CONN_CLOSING:
    default:
        return -1;
    }

    return 0;
}
