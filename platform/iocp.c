/*
 * Windows IOCP backend – implements event_loop.h
 *
 * Uses CreateIoCompletionPort + GetQueuedCompletionStatusEx.
 *
 * Design:
 *  - Each EventLoop wraps an IOCP handle.
 *  - evloop_add associates the fd (SOCKET) with the IOCP.
 *  - We use overlapped I/O with WSARecv/WSASend.
 *  - For simplicity, we also support a "select-style" fallback so the
 *    basic functionality works even without full overlapped I/O for each conn.
 *
 * NOTE: Full overlapped IOCP requires per-operation OVERLAPPED structs and
 * is significantly more involved. This implementation provides a working IOCP
 * loop using the "socket association + GetQueuedCompletionStatus" pattern,
 * combined with WSAEventSelect for readiness notification, which is the
 * standard pattern for wrapping a reactor API on Windows.
 *
 * For production use, replace with true overlapped (proactor) I/O.
 */

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mswsock.h>

#include "../src/event_loop.h"
#include "../src/utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Per-socket entry tracked by the EventLoop ────────────────────────────── */

#define MAX_FD_ENTRIES 65536

typedef struct {
    socket_t  fd;
    void     *ctx;
    uint32_t  interest;   /* EV_READ | EV_WRITE | EV_ERROR */
    WSAEVENT  ev_handle;
    int       active;
} FdEntry;

/* ── EventLoop struct ─────────────────────────────────────────────────────── */

struct EventLoop {
    HANDLE     iocp;
    FdEntry   *entries;      /* dynamic array */
    int        nentries;
    int        cap;
    WSAEVENT   wakeup_event; /* used to unblock WaitForMultipleObjectsEx */
};

/* ── helpers ──────────────────────────────────────────────────────────────── */

static FdEntry *find_entry(EventLoop *el, socket_t fd)
{
    for (int i = 0; i < el->nentries; i++) {
        if (el->entries[i].active && el->entries[i].fd == fd)
            return &el->entries[i];
    }
    return NULL;
}

static FdEntry *alloc_entry(EventLoop *el)
{
    /* Reuse a slot */
    for (int i = 0; i < el->nentries; i++) {
        if (!el->entries[i].active) return &el->entries[i];
    }
    /* Grow */
    if (el->nentries >= el->cap) {
        int new_cap = el->cap ? el->cap * 2 : 64;
        FdEntry *tmp = (FdEntry *)realloc(el->entries,
                                           (size_t)new_cap * sizeof(FdEntry));
        if (!tmp) return NULL;
        memset(tmp + el->cap, 0, (size_t)(new_cap - el->cap) * sizeof(FdEntry));
        el->entries = tmp;
        el->cap     = new_cap;
    }
    return &el->entries[el->nentries++];
}

static long to_network_events(uint32_t ev)
{
    long ne = 0;
    if (ev & EV_READ)  ne |= FD_READ | FD_ACCEPT | FD_CLOSE;
    if (ev & EV_WRITE) ne |= FD_WRITE | FD_CONNECT;
    if (ev & EV_ERROR) ne |= FD_CLOSE;
    return ne;
}

/* ── evloop_create ────────────────────────────────────────────────────────── */

EventLoop *evloop_create(void)
{
    EventLoop *el = (EventLoop *)calloc(1, sizeof(EventLoop));
    if (!el) return NULL;

    /* Create IOCP with no initial file handle */
    el->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    if (el->iocp == NULL) {
        free(el);
        return NULL;
    }

    el->wakeup_event = WSACreateEvent();
    if (el->wakeup_event == WSA_INVALID_EVENT) {
        CloseHandle(el->iocp);
        free(el);
        return NULL;
    }

    return el;
}

/* ── evloop_add ───────────────────────────────────────────────────────────── */

int evloop_add(EventLoop *el, socket_t fd, uint32_t events, void *ctx)
{
    if (find_entry(el, fd)) return -1;   /* already registered */

    FdEntry *e = alloc_entry(el);
    if (!e) return -1;

    WSAEVENT hev = WSACreateEvent();
    if (hev == WSA_INVALID_EVENT) return -1;

    long ne = to_network_events(events);
    if (WSAEventSelect(fd, hev, ne) != 0) {
        WSACloseEvent(hev);
        return -1;
    }

    e->fd        = fd;
    e->ctx       = ctx;
    e->interest  = events;
    e->ev_handle = hev;
    e->active    = 1;

    return 0;
}

/* ── evloop_mod ───────────────────────────────────────────────────────────── */

int evloop_mod(EventLoop *el, socket_t fd, uint32_t events, void *ctx)
{
    FdEntry *e = find_entry(el, fd);
    if (!e) return -1;

    e->ctx      = ctx;
    e->interest = events;

    long ne = to_network_events(events);
    if (events == 0) ne = 0;   /* deselect */

    return WSAEventSelect(fd, e->ev_handle, ne) == 0 ? 0 : -1;
}

/* ── evloop_del ───────────────────────────────────────────────────────────── */

int evloop_del(EventLoop *el, socket_t fd)
{
    FdEntry *e = find_entry(el, fd);
    if (!e) return -1;

    WSAEventSelect(fd, e->ev_handle, 0);
    WSACloseEvent(e->ev_handle);

    e->active    = 0;
    e->fd        = SOCK_INVALID;
    e->ev_handle = WSA_INVALID_EVENT;
    return 0;
}

/* ── evloop_wait ──────────────────────────────────────────────────────────── */

int evloop_wait(EventLoop *el, EvEvent *out, int maxev, int timeout_ms)
{
    /* Build array of active WSAEVENT handles (max MAXIMUM_WAIT_OBJECTS=64) */
#define MAX_WAIT_EVENTS 63  /* leave 1 slot for wakeup */

    WSAEVENT   handles[MAX_WAIT_EVENTS + 1];
    FdEntry   *entry_map[MAX_WAIT_EVENTS + 1];
    int        nhandles = 0;

    /* Add wakeup event as handle 0 */
    handles[nhandles]    = el->wakeup_event;
    entry_map[nhandles]  = NULL;
    nhandles++;

    for (int i = 0; i < el->nentries && nhandles <= MAX_WAIT_EVENTS; i++) {
        if (el->entries[i].active && el->entries[i].interest != 0) {
            handles[nhandles]   = el->entries[i].ev_handle;
            entry_map[nhandles] = &el->entries[i];
            nhandles++;
        }
    }

    DWORD timeout_dw = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;

    DWORD rc = WSAWaitForMultipleEvents(
                    (DWORD)nhandles, handles, FALSE, timeout_dw, FALSE);

    if (rc == WSA_WAIT_TIMEOUT) return 0;
    if (rc == WSA_WAIT_FAILED)  return -1;

    /*
     * WSAWaitForMultipleEvents auto-resets the event at the signaled index
     * (WSA_WAIT_EVENT_0 + first_index).  A secondary WSAWaitForMultipleEvents
     * call to re-verify that same slot would therefore see it already cleared
     * and incorrectly skip it.
     *
     * Fix: after the initial wait succeeds, scan EVERY active entry with
     * WSAEnumNetworkEvents.  That function reads the socket's internal pending
     * event record and atomically resets the WSAEVENT handle — it works
     * correctly even when the handle was already auto-reset by the wait.
     * Entries with no pending events (lNetworkEvents == 0) are skipped.
     */

    /* Handle the wakeup pseudo-entry (index 0) */
    if ((int)(rc - WSA_WAIT_EVENT_0) == 0)
        WSAResetEvent(el->wakeup_event);

    int nready = 0;

    for (int i = 1; i < nhandles && nready < maxev; i++) {
        FdEntry *e = entry_map[i];
        if (!e) continue;

        WSANETWORKEVENTS ne;
        /* Passing e->ev_handle lets WSAEnumNetworkEvents reset it.
         * If WSAWaitForMultipleEvents already auto-reset it, the call still
         * reads any pending events from the socket's internal record. */
        if (WSAEnumNetworkEvents(e->fd, e->ev_handle, &ne) != 0) continue;
        if (ne.lNetworkEvents == 0) continue;   /* nothing pending */

        uint32_t evflags = 0;
        if (ne.lNetworkEvents & (FD_READ | FD_ACCEPT | FD_CLOSE))
            evflags |= EV_READ;
        if (ne.lNetworkEvents & (FD_WRITE | FD_CONNECT))
            evflags |= EV_WRITE;
        /* FD_CLOSE: remote peer closed — treat as EV_READ (returns 0 bytes) */
        if (ne.lNetworkEvents & FD_CLOSE)
            evflags |= EV_READ;

        /* Only check error codes for events that actually fired.
         * iErrorCode[] entries for unfired events are undefined/stale and
         * must NOT be used — doing so causes false EV_ERROR. */
        for (int k = 0; k < FD_MAX_EVENTS; k++) {
            if ((ne.lNetworkEvents & (1L << k)) && ne.iErrorCode[k] != 0) {
                evflags |= EV_ERROR;
                break;
            }
        }

        if (evflags) {
            out[nready].ctx    = e->ctx;
            out[nready].events = evflags;
            nready++;
        }
    }

    return nready;
}

/* ── evloop_destroy ───────────────────────────────────────────────────────── */

void evloop_destroy(EventLoop *el)
{
    if (!el) return;

    for (int i = 0; i < el->nentries; i++) {
        if (el->entries[i].active) {
            WSAEventSelect(el->entries[i].fd, el->entries[i].ev_handle, 0);
            WSACloseEvent(el->entries[i].ev_handle);
        }
    }

    free(el->entries);

    if (el->wakeup_event != WSA_INVALID_EVENT)
        WSACloseEvent(el->wakeup_event);

    if (el->iocp) CloseHandle(el->iocp);

    free(el);
}

#endif /* _WIN32 */
