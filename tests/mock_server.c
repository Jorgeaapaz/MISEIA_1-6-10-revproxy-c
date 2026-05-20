/*
 * mock_server.c – minimal HTTP/1.1 server for integration tests.
 *
 * Usage: mock_server <port> <name>
 *
 * Responds to every HTTP request with:
 *   HTTP/1.1 200 OK
 *   Content-Length: <len>
 *   Connection: close
 *
 *   <name>
 *
 * The <name> identifies this backend in test assertions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET socket_t;
  #define SOCK_INVALID INVALID_SOCKET
  #define sock_close(s) closesocket(s)
  #define WOULD_BLOCK(e) ((e) == WSAEWOULDBLOCK)
  #define sock_errno() WSAGetLastError()
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  typedef int socket_t;
  #define SOCK_INVALID (-1)
  #define sock_close(s) close(s)
  #define WOULD_BLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
  #define sock_errno() errno
#endif

static void handle_client(socket_t cfd, const char *name)
{
    /* Read request (drain until \r\n\r\n) */
    char buf[4096];
    int  total = 0;
    int  found = 0;

    while (!found && total < (int)sizeof(buf) - 1) {
        int n = (int)recv(cfd, buf + total, sizeof(buf) - 1 - (size_t)total, 0);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n") || strstr(buf, "\n\n")) found = 1;
    }

    /* Build response */
    int   body_len = (int)strlen(name);
    char  resp[512];
    int   rlen = snprintf(resp, sizeof(resp),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        body_len, name);

    if (rlen > 0) {
        int sent = 0;
        while (sent < rlen) {
            int n = (int)send(cfd, resp + sent, (size_t)(rlen - sent), 0);
            if (n <= 0) break;
            sent += n;
        }
    }

    sock_close(cfd);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <name>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return 1;
    }

    const char *name = argv[2];

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    socket_t lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == SOCK_INVALID) {
        fprintf(stderr, "socket() failed\n");
        return 1;
    }

    int yes = 1;
#ifdef _WIN32
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
#else
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#   ifdef SO_REUSEPORT
    setsockopt(lfd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#   endif
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons((uint16_t)port);

    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "bind() failed for port %d: %s\n",
                port, strerror(errno));
        sock_close(lfd);
        return 1;
    }

    if (listen(lfd, 64) != 0) {
        fprintf(stderr, "listen() failed\n");
        sock_close(lfd);
        return 1;
    }

    printf("mock_server '%s' listening on port %d\n", name, port);
    fflush(stdout);

    for (;;) {
        struct sockaddr_in caddr;
#ifdef _WIN32
        int alen = sizeof(caddr);
#else
        socklen_t alen = sizeof(caddr);
#endif
        socket_t cfd = accept(lfd, (struct sockaddr *)&caddr, &alen);
        if (cfd == SOCK_INVALID) {
            int e = sock_errno();
            if (WOULD_BLOCK(e)) continue;
            fprintf(stderr, "accept() error: %d\n", e);
            break;
        }

        handle_client(cfd, name);
    }

    sock_close(lfd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
