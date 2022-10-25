//
// Created by John Morris on 10/20/22.
//

#ifndef FILTER_OPENSSL_H
#define FILTER_OPENSSL_H
#include "common/filter.h"

Filter *openSSLNew(Filter *next, char *cipher, Byte *key, size_t keyLen, Byte *iv, size_t ivLen);


#endif //FILTER_OPENSSL_H
