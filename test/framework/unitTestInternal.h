/*
 * A set of macros for creating simple unit tests.
 */

#ifndef FILTER_UNITTESTINTERNAL_H
#define FILTER_UNITTESTINTERNAL_H

#include <stdio.h>
#include <stdlib.h>

#define TEST_DIR "/tmp/pgtest/"

#define BEGIN do {
#define END   } while (0)

static const char *expectFmt = "Expected '%s' but got '%s'";

/* Verify two scaler values are equal */
#define PG_ASSERT_EQ(a,b)                                                                                              \
   BEGIN                                                                                                               \
       char _bufa[16], _bufb[16];                                                                                      \
       if ( (a > 0) != (b > 0) || (unsigned long long)a != (unsigned long long)b)                                      \
           PG_ASSERT_FMT(expectFmt, PG_INT_TO_STR(a, _bufa), PG_INT_TO_STR(b, _bufb));                                 \
   END


/* Verify two strings are equal */
#define PG_ASSERT_EQ_STR(stra, strb)                                                                                   \
    BEGIN                                                                                                              \
        if (strcmp(stra, strb) != 0)                                                                                   \
            PG_ASSERT_FMT(expectFmt, stra, strb);                                                                      \
    END

/* Format any integer as a string. */
#define PG_INT_TO_STR(a, bufa)                                                                                         \
    (a > 0)? (snprintf(bufa, sizeof(bufa), "%llu", (unsigned long long)a), bufa)                                       \
           : (snprintf(bufa, sizeof(bufa), "%lld",   (signed long long)a), bufa)

/* Verify there is no error */
#define PG_ASSERT_OK(error)  PG_ASSERT_ERROR(error, errorOK)

/* Verify there is an End Of File */
#define PG_ASSERT_EOF(error)   PG_ASSERT_ERROR(error, errorEOF);

/* Verify an expected error really occurred */
#define PG_ASSERT_ERROR(error, expectedError)                                                                          \
    BEGIN                                                                                                              \
        if (error.code != expectedError.code || strcmp(error.msg, expectedError.msg) != 0)                             \
            PG_ASSERT_FMT(expectFmt, expectedError.msg, error.msg);                               \
    END

#define PG_ASSERT_ERRNO(error, errno) \
    BEGIN                             \
	    PG_ASSERT(errorIsSystem(error)); \
		PG_ASSERT_EQ(error.code, -(errno)) ; \
    END

/* Display a formatted message and exit */
#define PG_ASSERT_FMT(fmt, ...)                                                                                        \
    BEGIN                                                                                                              \
        char _buf[256];                                                                                                \
        snprintf(_buf, sizeof(_buf), fmt, __VA_ARGS__);                                                                \
        PG_ASSERT_MSG(_buf);                                                                                           \
    END

/* Display an unformatted message and exit */
#define PG_ASSERT_MSG(msg)                                                                                             \
    (fprintf(stderr, "FAILED: %s (%s:%d) %s\n", __func__, __FILE__, __LINE__, msg ), abort())

/* Verify the expression is true */
#define PG_ASSERT(expr)                                                                                                \
    BEGIN                                                                                                              \
        if (!(expr))                                                                                                   \
            PG_ASSERT_MSG("'" #expr "' is false");                                                                     \
    END

static void beginTestGroup(char *name) {fprintf(stderr, "Begin Testgroup %s\n", name);}
static void beginTest(char *name) {fprintf(stderr, "    Test %s\n", name);}

#endif //FILTER_UNITTESTINTERNAL_H
