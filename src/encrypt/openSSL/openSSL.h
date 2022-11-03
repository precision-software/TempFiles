/* */
/* Created by John Morris on 10/20/22. */
/* */

#ifndef FILTER_OPENSSL_H
#define FILTER_OPENSSL_H
#include "common/stage.h"

Stage *openSSLNew(char *cipher, Byte *key, size_t keyLen, Byte *iv, size_t ivLen, Stage *next);


#endif /*FILTER_OPENSSL_H */
