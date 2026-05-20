#pragma once
#include "utils.h"
#include <stdint.h>

/* Create a TCP listening socket on the given port.
 * Sets SO_REUSEADDR, SO_REUSEPORT (Linux), non-blocking.
 * Returns the socket fd on success, SOCK_INVALID on error. */
socket_t listener_create(uint16_t port);
