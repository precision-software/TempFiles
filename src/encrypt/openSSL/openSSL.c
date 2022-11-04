/*
 * Data converter which implements OpenSSL encryption and decryption.
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
 * Converter structure for encrypting and decrypting OpenSSL.
 */
typedef struct OpenSSLConverter
{
    EVP_CIPHER_CTX *ctx;         /* SSL context. */
    char cipher[64];             /* The name of the cipher  (not used yet) */
    Byte key[64];                /* The cipher key. */
    Byte iv[64];                 /* The initialization vector. */
    int encrypt;                 /* 1 if encrypting, 0 if decrypting, matching libcrypto. */
} OpenSSLConverter;


/**
 * Start the encryption process. This function could produce header information
 * even before it encrypts data. Note the same function is used for both encryption
 * and decryption.
 */
size_t openSSLConverterBegin(OpenSSLConverter *this, Byte *buf, size_t bufSize, Error *error)
{
    /* Now we can set the key and initialization vector */
    this->ctx = EVP_CIPHER_CTX_new();
    const EVP_CIPHER *cipher = EVP_aes_256_cbc(); /* TODO: look up cipher, add error handling. */

    if (!EVP_CipherInit_ex2(this->ctx, cipher, this->key, this->iv, this->encrypt, NULL))
        return openSSLError(error);

    return 0;
}


/**
 * Process a block of data. The output buffer must contain enough room
 * to hold the entire result, so it is important the output size be as
 * large as the ConverterSize() function says.
 */
void openSSLConverterProcess(OpenSSLConverter *this, Byte *outBuf, size_t *outSize, Byte *inBuf, size_t *inSize, Error *error)
{
    int outlen = (int)*outSize;
    if (!EVP_CipherUpdate(this->ctx, outBuf, &outlen, inBuf, (int)*inSize))
        return (void)openSSLError(error);

    /* We assume the entire buffer was converted, so inSize is already set. */
    *outSize = outlen;
}

/**
 * Finalize the encryption/decryption stream. This function may flush
 * data or a trailer into the output buffer.
 */
size_t openSSLConverterEnd(OpenSSLConverter *this, Byte *outBuf, size_t outSize, Error *error)
{
    int outlen = (int)outSize;
    if (!EVP_CipherFinal_ex(this->ctx, outBuf, &outlen))
       return openSSLError(error);

    EVP_CIPHER_CTX_free(this->ctx);
    this->ctx = NULL;

    return outlen;
}

/**
 * Free up all allocated resources.
 */
void openSSLConverterFree(OpenSSLConverter *this, Error *error)
{
    if (this->ctx != NULL)
        EVP_CIPHER_CTX_free(this->ctx);
    free(this);

}


/**
 * Estimate the output size for a given input size.
 *  The size doesn't have to be exact, but it can't be smaller than
 *  any actual output size.
 */
size_t openSSLConverterSize(OpenSSLConverter *this, size_t inSize)
{
    /* OK to return larger than necessary. Add extra space for padding and digest. */
    /*   Editorial comment: OpenSSL should provide this function. */
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

/**
 * Create a new OpenSSL encryption/decryption converter.
 * @param cipher - the OpenSSL name of the cipher
 * @param encrypt - 1 for encryption, 0 for decryption.
 * @param key - the key used for encryption/decryption.
 * @param keyLen - the length of the key.
 * @param iv - initialization vector.
 * @param ivLen - length of the initialization vector
 * @return - a converter ready to start encrypting or decrypting data streams.
 */
Converter *openSSLConverterNew(char *cipher, bool encrypt, Byte *key, size_t keyLen, Byte *iv, size_t ivLen)
{
    OpenSSLConverter *this = malloc(sizeof(OpenSSLConverter));

    assert(strlen(cipher) < sizeof(this->cipher));
    assert(keyLen <= sizeof(this->key));
    assert(ivLen <= sizeof(this->iv));

    /* Save the configuration in our structure. */
    this->ctx = NULL;
    this->encrypt = encrypt ? 1 : 0;
    strcpy(this->cipher, cipher);
    memcpy(this->key, key, keyLen);
    memcpy(this->iv, iv, ivLen);

    // Create the corresponding converter object.
    return converterNew(this, &openSSLConverterInterface);
}

/**
 * Create a filter for encrypting/decrypting file streams using OpenSSL.
 * @param cipher - the OpenSSL name of the cipher
 * @param encrypt - 1 for encryption, 0 for decryption.
 * @param key - the key used for encryption/decryption.
 * @param keyLen - the length of the key.
 * @param iv - initialization vector.
 * @param ivLen - length of the initialization vector
 * @return - a filter ready to start encrypting or decrypting file data
 */
Filter *openSSLNew(char *cipherName, Byte *key, size_t keyLen, Byte *iv, size_t ivLen, Filter *next)
{
    /* Create a filter which encrypts during writes and decrypts during reads. */
    Converter *encrypt = openSSLConverterNew(cipherName, true, key, keyLen, iv, ivLen);
    Converter *decrypt = openSSLConverterNew(cipherName, false, key, keyLen, iv, ivLen);
    return convertFilterNew(16*1024, encrypt, decrypt, next);
}
