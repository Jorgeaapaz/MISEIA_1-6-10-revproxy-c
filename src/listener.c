#include "listener.h"
#include "log.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netinet/tcp.h>
  #include <unistd.h>
#endif

socket_t listener_create(uint16_t port)
{
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == SOCK_INVALID) {
        LOG_ERROR("socket() failed for port %u", (unsigned)port);
        return SOCK_INVALID;
    }

    /* SO_REUSEADDR */
    int yes = 1;
#ifdef _WIN32
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
#else
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#   ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#   endif
    /* Disable Nagle for listener (inherited by accepted sockets on some systems) */
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LOG_ERROR("bind() failed for port %u: %s", (unsigned)port, strerror(errno));
        sock_close(fd);
        return SOCK_INVALID;
    }

    if (listen(fd, SOMAXCONN) != 0) {
        LOG_ERROR("listen() failed for port %u: %s", (unsigned)port, strerror(errno));
        sock_close(fd);
        return SOCK_INVALID;
    }

    if (sock_set_nonblocking(fd) != 0) {
        LOG_ERROR("sock_set_nonblocking() failed for port %u", (unsigned)port);
        sock_close(fd);
        return SOCK_INVALID;
    }

    LOG_INFO("Listening on port %u", (unsigned)port);
    return fd;
}
