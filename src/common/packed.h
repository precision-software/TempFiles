/*
 * A collection of helper routines for packing/unpacking data in a buffer.
 *   - Quick, especially when inlined.
 *   - Minimal error checking to not overflow buffer.
 *   - All bytes are in stored "network" (big endian) order.
 *
 * Example of use:
 *     Byte buffer[256], *bp = buffer;
 *     pack8(&bp, 42);
 *     packstr(&bp, "Hello World!");
 *     pack64(&bp, 42);
 *
 * Apply "unpack" routines in same order to unpack the values.
 *     char str[BIG_ENOUGH];
 *     bp = buffer;
 *     xx = unpack8(&bp);
 *     unpackstr(&bp, str);
 *     yy = unpack64(&bp);
 *
 * Note the first param is a reference to the pointer, allowing the pointer to be updated.
 * This double referencing is optimized away when inlined.
 */
#ifndef FILTER_PACKED_H
#define FILTER_PACKED_H

#include "common/error.h"  /* for Byte and size_t. */

/* Save a byte and bump the pointer. */
inline static void pack1(Byte **bp, Byte *end, size_t val)
{
    if (*bp < end)
        **bp = val;
    if (*bp <= end)
        (*bp)++;
}

/* Grab a byte and bump the pointer. */
inline static size_t unpack1(Byte **bp, Byte *end)
{
    Byte b = 0;
    if (*bp < end)
        b = **bp;
    if (*bp <= end)
        (*bp)++;
    return b;
}

inline static void pack2(Byte **bp, Byte *end, size_t val)
{
    pack1(bp, end, val>>8);
    pack1(bp, end, val);
}

inline static size_t unpack2(Byte **bp, Byte *end)
{
    return unpack1(bp, end)<<8 | unpack1(bp, end);
}

inline static void pack4(Byte **bp, Byte *end, size_t val)
{
    pack2(bp, end, val>>16);
    pack2(bp, end, val);
}

inline static size_t unpack4(Byte **bp, Byte *end)
{
    return unpack2(bp, end)<<16 | unpack2(bp, end);
}

inline static void pack8(Byte **bp, Byte *end, size_t val)
{
    pack4(bp, end, val>>32);
    pack4(bp, end, val);
}

inline static size_t unpack8(Byte **bp, Byte *end)
{
    return unpack4(bp, end)<<32 | unpack4(bp, end);
}

/* save a zero terminated string and bump the pointer. */
inline static void packstr(Byte **bp, Byte *end, char *str)
{
    char c;
    do {
        c = *str++;
        pack1(bp, end, c);
    } while (c != '\0');
}

/* Grab a zero terminated string and bump the pointer */
inline static void unpackstr(Byte **bp, Byte *end, char *str)
{
    char c;
    do {
        c = (char)unpack1(bp, end);
        *str++ = c;
    } while (c != '\0');
}

/* Move bytes out of the buffer and update pointer */
inline static void unpackBytes(Byte **bp, Byte *end, Byte *bytes, size_t size)
{
    for (int idx = 0; idx < size; idx++)
        bytes[idx] = unpack1(bp, end);
}

/* Store bytes in the buffer and update the pointer */
inline static void packBytes(Byte **bp, Byte *end, Byte *bytes, size_t size)
{
    for (int idx = 0; idx < size; idx++)
        pack1(bp, end, bytes[idx]);
}


#endif //FILTER_PACKED_H
