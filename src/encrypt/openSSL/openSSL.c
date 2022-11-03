/*
 *
 */

#include <openssl/evp.h>
#include <openssl/err.h>

#include "encrypt/openSSL/openSSL.h"
#include "common/convertFilter.h"

static const Error errorBadKeyLen = (Error){.code=errorCodeFilter, .msg="Unexpected Key or IV length."};
static const Error errorBadDecryption = (Error) {.code=errorCodeFilter, .msg="Unable to decrypt current buffer"};

/*
 * Convenience function to build an SSL error and return zero.
 */
static size_t openSSLError(Error *error)
{
    if (errorIsOK(*error))
        *error = (Error){.code=errorCodeFilter, .msg=ERR_error_string(ERR_get_error(), NULL)};
    return 0;
}


/**
 * Converter to encrypt and decrypt SSL.
 */
typedef struct OpenSSLConverter
{
    EVP_CIPHER_CTX *ctx;         /* SSL context. */
    char cipher[64];             /* The name of the cipher  (not used yet) */
    Byte key[64];                /* The cipher key. */
    Byte iv[64];                 /* The initialization vector. */
    int encrypt;                 /* 1 if encrypting, 0 if decrypting, matching libcrypto. */
} OpenSSLConverter;


size_t openSSLConverterBegin(OpenSSLConverter *this, Byte *buf, size_t bufSize, Error *error)
{
    /* Now we can set the key and initialization vector */
    this->ctx = EVP_CIPHER_CTX_new();
    const EVP_CIPHER *cipher = EVP_aes_256_cbc(); /* TODO: look up cipher, add error handling. */

    if (!EVP_CipherInit_ex2(this->ctx, cipher, this->key, this->iv, this->encrypt, NULL))
        return openSSLError(error);

    return 0;
}


void openSSLConverterProcess(OpenSSLConverter *this, Byte *outBuf, size_t *outSize, Byte *inBuf, size_t *inSize, Error *error)
{
    int outlen = (int)*outSize;
    if (!EVP_CipherUpdate(this->ctx, outBuf, &outlen, inBuf, (int)*inSize))
        return (void)openSSLError(error);

    /* We assume the entire buffer was converted, so inSize is already set. */
    *outSize = outlen;
}


size_t openSSLConverterEnd(OpenSSLConverter *this, Byte *outBuf, size_t outSize, Error *error)
{
    int outlen = (int)outSize;
    if (!EVP_CipherFinal_ex(this->ctx, outBuf, &outlen))
       return openSSLError(error);

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
    /* OK to return larger than necessary. Add extra space for padding and checksum. */
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
