//
// Created by John Morris on 10/19/22.
//

#ifndef FILTER_UNITTESTINTERNAL_H
#define FILTER_UNITTESTINTERNAL_H

#include <stdio.h>

#define TEST_DIR "/tmp/pgtest/"

#define BEGIN do {
#define END   } while (0)

static const char *expectFmt = "Expected '%s' but got '%s'";

#define PG_ASSERT_EQ(a,b)                                                                                              \
   BEGIN                                                                                                               \
       char _bufa[16], _bufb[16];                                                                                      \
       if ( (a > 0) != (b > 0) || (unsigned long long)a != (unsigned long long)b)                                      \
           PG_ASSERT_FMT(expectFmt, PG_INT_TO_STR(a, _bufa), PG_INT_TO_STR(b, _bufb));                                 \
   END

#define PG_ASSERT_EQ_STR(stra, strb)                                                                                   \
    BEGIN                                                                                                              \
        if (strcmp(stra, strb) != 0)                                                                                   \
            PG_ASSERT_FMT(expectFmt, stra, strb);                                                                      \
    END

#define PG_INT_TO_STR(a, bufa)                                                                                         \
    (a > 0)? (snprintf(bufa, sizeof(bufa), "%llu", (unsigned long long)a), bufa)                                       \
           : (snprintf(bufa, sizeof(bufa), "%lld",   (signed long long)a), bufa)

#define PG_ASSERT_OK(error)  PG_ASSERT_ERROR(error, errorOK)

#define PG_ASSERT_EOF(error)   PG_ASSERT_ERROR(error, errorEOF);


#define PG_ASSERT_ERROR(error, expectedError)                                                                          \
    BEGIN                                                                                                              \
        if (error.code != expectedError.code || strcmp(error.msg, expectedError.msg) != 0)                             \
            PG_ASSERT_FMT(expectFmt, expectedError.msg, error.msg);                               \
    END

#define PG_ASSERT_FMT(fmt, ...)                                                                                        \
    BEGIN                                                                                                              \
        char _buf[256];                                                                                                 \
        snprintf(_buf, sizeof(_buf), fmt, __VA_ARGS__);                                                                       \
        PG_ASSERT_MSG(_buf);                                                                                            \
    END

#define PG_ASSERT_MSG(msg)                                                                                             \
    (fprintf(stderr, "FAILED: %s (%s:%d) %s\n", __func__, __FILE__, __LINE__, msg ), abort())

#define PG_ASSERT(expr)                                                                                                \
    BEGIN                                                                                                              \
        if (!(expr))                                                                                                   \
            PG_ASSERT_MSG("'" #expr "' is false");                                                            \
    END

static void beginTestGroup(char *name) {fprintf(stderr, "Begin Testgroup %s\n", name);}
static void beginTest(char *name) {fprintf(stderr, "    Test %s\n", name);}

#endif //FILTER_UNITTESTINTERNAL_H
