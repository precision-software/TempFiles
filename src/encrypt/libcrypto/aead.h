/*
 *
 */
#ifndef FILTER_AEAD_H
#define FILTER_AEAD_H
#include "common/filter.h"

typedef struct AeadFilter AeadFilter;
AeadFilter *aeadFilterNew(char *cipherName, size_t blockSize, Byte *key, size_t keyLen, void *next);

#endif //FILTER_AEAD_H
