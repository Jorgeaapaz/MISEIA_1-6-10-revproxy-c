#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET socket_t;
  #define SOCK_INVALID  INVALID_SOCKET
  #define sock_close(s) closesocket(s)
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  typedef int socket_t;
  #define SOCK_INVALID  (-1)
  #define sock_close(s) close(s)
#endif

/* Monotonic clock in milliseconds */
int64_t time_ms(void);

/* Set socket to non-blocking mode. Returns 0 on success, -1 on error. */
int sock_set_nonblocking(socket_t fd);

/* Parse "host:port" string.
 * Fills host_out (up to hlen bytes, null-terminated) and *port_out.
 * Returns 0 on success, -1 on error. */
int parse_hostport(const char *s, char *host_out, size_t hlen, int *port_out);

/* Safe bounded string copy – always null-terminates dst.
 * Returns number of chars copied (excluding null). */
size_t strlcpy_safe(char *dst, const char *src, size_t size);

/* Convert ASCII string to lowercase in-place */
void str_tolower(char *s);

/* Strip trailing whitespace (space, \t, \r, \n) in-place */
void str_rtrim(char *s);

/* Portable errno for socket operations */
#ifdef _WIN32
  #define sock_errno() WSAGetLastError()
#else
  #include <errno.h>
  #define sock_errno() errno
#endif
