/* */
/* Created by John Morris on 10/20/22. */
/* */

#ifndef FILTER_ENCRYPT_H
#define FILTER_ENCRYPT_H
#include "common/filter.h"

Filter *openSSLNew(char *cipher, Byte *key, size_t keyLen, Byte *iv, size_t ivLen, Filter *next);


#endif /*FILTER_ENCRYPT_H */
