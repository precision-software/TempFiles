/*
 * A collection of helper routines for packing/unpacking data in a buffer.
 *   - Quick, especially when inlined.
 *   - No error checking at all.  BEWARE!
 *   - All bytes are in stored "network" (big endian) order.
 *
 * Example of use:
 *     Byte buffer[256], *bp = buffer;
 *     put8(&bp, 42);
 *     putstr(&bp, "Hello World!");
 *     put64(&bp, 42);
 *
 * Apply "get" routines in same order to unpack the values.
 *     char str[BIG_ENOUGH];
 *     bp = buffer;
 *     xx = get8(&bp);
 *     getstr(&bp, str);
 *     yy = get64(&bp);
 *
 * Note the first param is a reference to the pointer, allowing the pointer to be updated.
 * This double referencing is optimized away when inlined.
 */
#ifndef FILTER_GETPUT_H
#define FILTER_GETPUT_H

#include "common/error.h"  /* for Byte and size_t. */

/* Save a byte and bump the pointer. */
inline static void put8(Byte **bp, size_t val)
{
    *(*bp++) = val;
}

/* Grab a byte and bump the pointer. */
inline static size_t get8(Byte **bp)
{
    return *(*bp++);
}

inline static void put16(Byte **bp, size_t val)
{
    put8(bp, val>>8);
    put8(bp, val);
}

inline static size_t get16(Byte **bp)
{
    return get8(bp)<<8 | get8(bp);
}

inline static void put32(Byte **bp, size_t val)
{
    put16(bp, val>>16);
    put16(bp, val)
}

inline static size_t get32(Byte **bp)
{
    return get16(bp)<<16 | get16(bp);
}

inline static void put64(Byte **bp, size_t val)
{
    put32(bp, val>>32);
    put32(bp, val);
}

inline static size_t get64(Byte **bp)
{
    return get32(bp)<<32 | get32(bp);
}

/* save a zero terminated string and bump the pointer. */
inline static void putstr(Byte **bp, char *str)
{
    char c;
    do {
        c = *str++;
        put8(bp, c);
    } while (c != '\0');
}

/* Grab a zero terminated string and bump the pointer */
inline static void getstr(Byte **bp, char *str)
{
    char c;
    do {
        c = get8(bp);
        *str++ = c;
    } while (c != '\0');
}

/* Move bytes out of the buffer and update pointer */
inline static void putBytes(Byte **bp, Byte *bytes, size_t size)
{
    memcpy(*bp, bytes, size);
    *bp += size;
}

/* Store bytes in the buffer and update the pointer */
inline static void getBytes(Byte **bp, Byte *bytes, size_t size)
{
    memcpy(bytes, *bp, size);
    *bp += size;
}


#endif //FILTER_GETPUT_H
