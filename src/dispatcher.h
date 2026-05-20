#pragma once
#include <stddef.h>
#include <stdint.h>

/* Scan HTTP header bytes already accumulated in buf[0..len-1].
 *
 * Returns:
 *   1  – Host header found; domain_out is filled (null-terminated, lower-case,
 *          port stripped).
 *   0  – Need more data (Host not yet seen; header not yet complete).
 *  -1  – Error or limit exceeded (buffer full without finding \r\n\r\n, or
 *          Host header value is missing/malformed).
 */
int dispatcher_feed(const uint8_t *buf, int len,
                    char *domain_out, size_t domain_max);
