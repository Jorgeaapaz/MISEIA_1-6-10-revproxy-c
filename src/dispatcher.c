#include "dispatcher.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* Maximum header size (8 KiB per spec REQ-C-02 / REQ-F-05) */
#define MAX_HEADER_SIZE 8192

int dispatcher_feed(const uint8_t *buf, int len,
                    char *domain_out, size_t domain_max)
{
    if (!buf || len <= 0 || !domain_out || domain_max == 0) return -1;

    /* Check if we've exceeded the header size limit */
    if (len > MAX_HEADER_SIZE) return -1;

    /* Scan for \r\n\r\n (end of headers) */
    int header_end = -1;
    for (int i = 0; i <= len - 4; i++) {
        if (buf[i]   == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n') {
            header_end = i + 4;
            break;
        }
    }

    /* Also accept \n\n (some clients) */
    if (header_end < 0) {
        for (int i = 0; i <= len - 2; i++) {
            if (buf[i] == '\n' && buf[i+1] == '\n') {
                header_end = i + 2;
                break;
            }
        }
    }

    /* If buffer is full (8 KiB) and we still haven't seen end of headers */
    if (header_end < 0 && len >= MAX_HEADER_SIZE) return -1;

    /* If we haven't found end of headers yet, need more data */
    if (header_end < 0) return 0;

    /* Scan header lines for "Host:" */
    const char *hdr   = (const char *)buf;
    int         limit = header_end;

    /* Skip the request line (first line) */
    int pos = 0;
    while (pos < limit && hdr[pos] != '\n') pos++;
    if (pos < limit) pos++;   /* skip '\n' */

    while (pos < limit) {
        /* Find end of this header line */
        int line_start = pos;
        while (pos < limit && hdr[pos] != '\n') pos++;
        int line_end = pos;
        if (pos < limit) pos++;   /* skip '\n' */

        /* Trim trailing \r */
        int llen = line_end - line_start;
        while (llen > 0 && (hdr[line_start + llen - 1] == '\r' ||
                             hdr[line_start + llen - 1] == '\n')) {
            llen--;
        }

        if (llen <= 0) continue;   /* blank line = end of headers */

        /* Case-insensitive check for "Host:" */
        if (llen >= 5 &&
            tolower((unsigned char)hdr[line_start])   == 'h' &&
            tolower((unsigned char)hdr[line_start+1])  == 'o' &&
            tolower((unsigned char)hdr[line_start+2])  == 's' &&
            tolower((unsigned char)hdr[line_start+3])  == 't' &&
            hdr[line_start + 4] == ':') {

            /* Extract value after "Host:" */
            int val_start = line_start + 5;
            /* Skip whitespace */
            while (val_start < line_start + llen &&
                   (hdr[val_start] == ' ' || hdr[val_start] == '\t')) {
                val_start++;
            }

            int val_len = (line_start + llen) - val_start;
            if (val_len <= 0) return -1;   /* Empty Host header */

            /* Copy to domain_out, normalize to lower-case */
            size_t copy_len = (size_t)val_len < domain_max - 1
                              ? (size_t)val_len
                              : domain_max - 1;
            for (size_t i = 0; i < copy_len; i++) {
                domain_out[i] = (char)tolower(
                    (unsigned char)hdr[val_start + (int)i]);
            }
            domain_out[copy_len] = '\0';

            /* Strip port if present (e.g. "example.com:8080") */
            char *colon = strrchr(domain_out, ':');
            if (colon) {
                /* Only strip if what follows looks like a port number */
                int looks_like_port = 1;
                for (char *cp = colon + 1; *cp; cp++) {
                    if (!isdigit((unsigned char)*cp)) { looks_like_port = 0; break; }
                }
                if (looks_like_port) *colon = '\0';
            }

            return 1;
        }
    }

    /* End of headers reached without finding Host: */
    return -1;
}
