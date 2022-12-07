/*
 *
 */
#ifndef FILTER_DEBUG_H
#define FILTER_DEBUG_H

#include "common/filter.h"

//#define debug printf
#define debug if (1) ; else (void)

static inline char *asHex(Byte *buf, size_t size)
{
    static char hex[1024];
    static char *bp = hex;
    if (size > 64)
        size = 64;

    /* Wrap around - we can provide a limited number of hex conversion in one printf */
    if (bp + 2 * size > hex + sizeof(hex) - 1)
        bp = hex;

    char *start = bp;

    for (int i = 0; i < size; i++)
        bp += sprintf(bp, "%.2x", buf[i]);
    bp++; /* final null char */

    return start;
}


#endif //FILTER_DEBUG_H
