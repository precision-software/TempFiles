/*
 *
 */
#ifndef FILTER_DEBUG_H
#define FILTER_DEBUG_H

#include "common/filter.h"

#define debug printf
//#define debug if (1) ; else (void)

/*
 * Quick and dirty debug function to display a buffer in hex.
 * Avoids memory allocation by reusing portions of static buffer.
 */
static inline char *asHex(Byte *buf, size_t size)
{
    static char hex[1024];
    static char *bp = hex;

    /* Truncate the output to 64 bytes */
    if (size > 64)
        size = 64;

    /* Wrap around - we can provide a limited number of hex conversion in one printf */
    if (bp + 2 * size > hex + sizeof(hex) - 1)
        bp = hex;

    /* Convert bytes to hex and add to static buffer */
    char *start = bp;
    for (int i = 0; i < size; i++)
        bp += sprintf(bp, "%.2x", buf[i]);
    bp++; /* final null char added by sprintf */

    /* point to where the hex string started */
    return start;
}


#endif //FILTER_DEBUG_H
