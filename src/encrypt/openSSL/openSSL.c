//
// Created by John Morris on 10/20/22.
//

//
// Created by John Morris on 10/20/22.
//

#include <openssl/evp.h>
#include <openssl/err.h>

#include "encrypt/openSSL/openSSL.h"
#include "common/convertFilter.h"

static const Error errorBadKeyLen = (Error){.code=errorCodeFilter, .msg="Unexpected Key or IV length."};
static const Error errorBadDecryption = (Error) {.code=errorCodeFilter, .msg="Unable to decrypt current buffer"};
static Error openSSLError(void) {return (Error){.code=errorCodeFilter, .msg=ERR_error_string(ERR_get_error(), NULL)};}


typedef struct OpenSSLConverter
{
    EVP_CIPHER_CTX *ctx;
    char cipher[64];
    Byte key[64];
    Byte iv[64];
    int encrypt;
} OpenSSLConverter;

// Note: generates prelude, but doesn't consume it.
size_t openSSLConverterBegin(OpenSSLConverter *this, Error *error)
{
    if (isError(*error)) return 0;

    /* Now we can set the key and initialization vector */
    this->ctx = EVP_CIPHER_CTX_new();
    const EVP_CIPHER *cipher = EVP_aes_256_cbc(); // TODO: look up cipher, add error handling.

    if (!EVP_CipherInit_ex2(this->ctx, cipher, this->key, this->iv, this->encrypt, NULL))
        *error = openSSLError();

    return 0;
}

void openSSLConverterProcess(OpenSSLConverter *this, Byte *outBuf, size_t *outSize, Byte *inBuf, size_t *inSize, Error *error)
{
    int outlen = (int)*outSize;
    if (!EVP_CipherUpdate(this->ctx, outBuf, &outlen, inBuf, (int)*inSize))
    {
        *error = openSSLError();
        *inSize = *outSize = 0;
        return;
    }

    // We assume the entire buffer was converted, so inSize doesn't change.
    *outSize = outlen;
}

// Note: generates trailer, but doesn't consume it.
size_t openSSLConverterEnd(OpenSSLConverter *this, Byte *outBuf, size_t outSize, Error *error)
{
    if (isError(*error)) return 0;

    int outlen = (int)outSize;
    if (!EVP_CipherFinal_ex(this->ctx, outBuf, &outlen))
    {
        *error = openSSLError();
        return 0;
    }

    EVP_CIPHER_CTX_free(this->ctx);
    this->ctx = NULL;

    return outlen;
}

void openSSLConverterFree(OpenSSLConverter *this, Error *error)
{
    if (this->ctx != NULL)
        EVP_CIPHER_CTX_free(this->ctx);
    free(this);

}

size_t openSSLConverterSize(OpenSSLConverter *this, size_t inSize)
{
    // OK to return larger than necessary. Add extra space for padding and checksum.
    return inSize + 2*EVP_MAX_BLOCK_LENGTH + EVP_MAX_MD_SIZE;
}

ConverterIface openSSLConverterInterface = (ConverterIface)
        {
                .fnBegin = (ConvertBeginFn)openSSLConverterBegin,
                .fnConvert = (ConvertConvertFn) openSSLConverterProcess,
                .fnEnd = (ConvertEndFn)openSSLConverterEnd,
                .fnFree = (ConvertFreeFn)openSSLConverterFree,
                .fnSize = (ConvertSizeFn)openSSLConverterSize,
        };


Converter *openSSLConverterNew(char *cipher, bool encrypt, Byte *key, size_t keyLen, Byte *iv, size_t ivLen)
{
    OpenSSLConverter *this = malloc(sizeof(OpenSSLConverter));

    assert(strlen(cipher) < sizeof(this->cipher));
    assert(keyLen <= sizeof(this->key));
    assert(ivLen <= sizeof(this->iv));

    this->ctx = NULL;
    this->encrypt = encrypt ? 1 : 0;
    strcpy(this->cipher, cipher);
    memcpy(this->key, key, keyLen);
    memcpy(this->iv, iv, ivLen);

    return converterNew(this, &openSSLConverterInterface);
}


Filter *openSSLNew(char *cipherName, Byte *key, size_t keyLen, Byte *iv, size_t ivLen, Filter *next)
{
    Converter *encrypt = openSSLConverterNew(cipherName, true, key, keyLen, iv, ivLen);
    Converter *decrypt = openSSLConverterNew(cipherName, false, key, keyLen, iv, ivLen);
    return convertFilterNew(16*1024, encrypt, decrypt, next);
}
