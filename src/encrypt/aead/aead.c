/*
 * TODO: not an issue yet, but Seek() will need a way of setting the blockNr.
 * TODO: when rewriting a random block, we will re-use the same key+nonce which is a security issue.
 * TODO: what about adding a version number to the record?  Nonce space starts to get small however.
 *
 * Currently, we pad the last cipherblock in a record.
 * Alternative is to pad the last record, and don't pad the cipherblocks.
 * Preferable, esp if the MACs and header are kept separately, since it maintains block alignmant.
 */
#include "encrypt/aead/aead.h"


#include <openssl/opensslv.h>
#include <openssl/ssl.h>

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include "encrypt/openSSL/encrypt.h"
#include "common/convertFilter.h"
#include "common/filter.h"
#include "common/getput.h"

/* Forward references */
static const Error errorBadKeyLen = (Error){.code=errorCodeFilter, .msg="Unexpected Key or IV length."};
static const Error errorBadDecryption = (Error) {.code=errorCodeFilter, .msg="Unable to decrypt current buffer"};
static size_t openSSLError(Error *error);
void generateNonce(Byte *nonce, Byte *iv, size_t ivSize, size_t seqNr);

void aeadEncrypt(AeadConverter *this, Byte *outBuf, size_t *outSize, Byte *inBuf, size_t *inSize, Error *error);
void aeadDecrypt(AeadConverter *this, Byte *outBuf, size_t *outSize, Byte *inBuf, size_t *inSize, Error *error);

size_t aead_encrypt(AeadConverter *this, Byte *plainText, size_t plainSize, Byte *header,
                  size_t headerSize, Byte *cipherText, size_t cipherSize,Byte *tag, Error *error);

size_t aead_decrypt(AeadConverter *this, Byte *plainText, size_t plainSize, Byte *header,
                  size_t headerSize, Byte *cipherText, size_t cipherSize, Byte *tag, Error *error);

/**
 * Converter structure for encrypting and decrypting TLS Records.
 */
#define MAX_CIPHER_NAME 64
struct AeadConverter
{
    /* Configuration */
    char cipherName[MAX_CIPHER_NAME];           /* The name of the cipher */
    Byte key[EVP_MAX_KEY_LENGTH];  /* The key for encrypting/decrypting */
    size_t blockSize;              /* The maximum plaintext block size */
    size_t cipherSize;             /* The corresponding ciphertext size */

    /* Cipher State */
    size_t keySize;              /* The size of the key in bytes */
    size_t ivSize;               /* Size of the initialization vector, typically same as blockSize */
    size_t cipherBlockSize;      /* Size of the cipher block. (typically 16 bytes for AES) */
    size_t tagSize;              /* Size of the MAC tag to authenticate the encrypted record. */
    bool hasPadding;             /* Is the last cipher block filled with padding? */
    EVP_CIPHER_CTX *ctx;         /* Libcrypto context. */
    EVP_CIPHER *cipher;          /* The libcrypto cipher structure */

    /* Our state */
    int encrypt;                 /* 1 if encrypting, 0 if decrypting, matching libcrypto. */
    size_t blockNr;              /* The record sequence number, starting at 0 and incrementing. */
    size_t previousSize;         /* size of last plaintext */
    Byte iv[EVP_MAX_IV_LENGTH];  /* The initialization vector for the sequence of records. */
};

const size_t MAX_AEAD_HEADER_SIZE = 2 + 4 + 4 + MAX_CIPHER_NAME + EVP_MAX_IV_LENGTH + EVP_MAX_MD_SIZE;
/**
 * Helper function to start encryption/decryption. This sets up the libcrypto library.
 */
static void
aeadCipherSetup(AeadConverter *this, Error *error)
{
    /* Create an OpenSSL cipher context. */
    this->ctx = EVP_CIPHER_CTX_new();

    /* Lookup cipher by name. The name must be an exact match to an OpenSSL name */
    //this->cipher = EVP_CIPHER_fetch(NULL, this->cipherName, NULL);
    this->cipher = EVP_aes_256_gcm(); /* TODO: Temporary */
    if (this->cipher == NULL)
        return (void) filterError(error, "Encryption problem - cipher name not recognized");

    /* Get the properties of the selected cipher */
    this->ivSize = EVP_CIPHER_iv_length(this->cipher);
    this->keySize = EVP_CIPHER_key_length(this->cipher);
    this->cipherBlockSize = EVP_CIPHER_block_size(this->cipher);
    this->hasPadding = true;
    this->tagSize = 16;
}

/*
 * Start encrypting a stream of plaintext records.
 * It generates a file header with information to assist future decryption.
 */
size_t aeadEncryptBegin(AeadConverter *this, Byte *outBuf, size_t *outSize, Error *error)
{
    *error = errorOK;

    /* Set up the encryption cipher */
    aeadCipherSetup(this, error);
    this->encrypt = true;

    /* We are starting afresh. Generate unique initialization vector. */
    this->blockNr = 0;
    RAND_bytes(this->iv, this->ivSize);
    this->previousSize = 0;

    return 0;
}

/*
 * Encrypt the next block of plaintext.
 * The encrypted block will be slightly larger due to
 *   1) Padding.   (allows block sizes which are not multiples of the cipher's block size.)
 *   2) MAC tag.
 */
void aeadEncryptProcess(AeadConverter *this, Byte *outBuf, size_t *outSize, Byte *inBuf, size_t *inSize, Error *error)
{
    /* Encrypt the input buffer. We don't have additional data to validate. */
    Byte tag[EVP_MAX_MD_SIZE];
    *outSize = aead_encrypt(this, inBuf, *inSize, NULL, 0, outBuf, *outSize - this->tagSize, tag, error);
    if (isError(*error))
        return;

    /* Append the tag to the cipher text. */
    memcpy(outBuf + *outSize, tag, this->tagSize);
    *outSize += this->tagSize;

    /* Keep track of this block size so we know if we are at the end. (always ends on partial block) */
    this->blockNr++;
    this->previousSize = *inSize;
}


/**
 * Finalize the encryption stream.
 */
size_t aeadEncryptEnd(AeadConverter *this, Byte *outBuf, size_t outSize, Error *error)
{
    /* We always end on a partial block. If last block was full, then create an empty one */
    if (this->previousSize == this->blockSize)
        aeadEncryptProcess(this, outBuf, &outSize, NULL, 0, error);
    else
        outSize = 0;

    EVP_CIPHER_free(this->cipher);
    EVP_CIPHER_CTX_free(this->ctx);
    this->ctx = NULL;
    this->cipher = NULL;

    return outSize;
}

/**
 * Free up all allocated resources.
 */
void aeadFree(AeadConverter *this, Error *error)
{
    if (this->ctx != NULL)
        EVP_CIPHER_CTX_free(this->ctx);
    if (this->cipher != NULL)
        EVP_CIPHER_free(this->cipher);
    free(this);
}


/**
 * Estimate the output size for a given input size.
 *  The size doesn't have to be exact, but it can't be smaller than
 *  the actual output size. Ideally, libcrypto would provide this function.
 */
size_t aeadEncryptSize(AeadConverter *this, size_t inSize)
{
    /* OK to return larger than necessary. Add extra space for padding and digest. */
    return inSize + 2*EVP_MAX_BLOCK_LENGTH + EVP_MAX_MD_SIZE;
}

ConverterIface aeadEncryptInterface = (ConverterIface)
        {
                .fnBegin = (ConvertBeginFn)aeadEncryptBegin,
                .fnConvert = (ConvertConvertFn) aeadEncryptProcess,
                .fnEnd = (ConvertEndFn)aeadEncryptEnd,
                .fnFree = (ConvertFreeFn)aeadConverterFree,
                .fnSize = (ConvertSizeFn)aeadEncryptSize,
        };

/**
 * Create a new OpenSSL encryption converter.
 * @param cipher - the OpenSSL name of the cipher
 * @param key - the key used for encryption/decryption.
 * @param keyLen - the length of the key.
 * @return - a converter ready to start encrypting or decrypting data streams.
 */
Converter *aeadEncryptNew(char *cipher, size_t blockSize, Byte *key, size_t keyLen)
{
    AeadConverter *this = malloc(sizeof(AeadConverter));      // TODO: Postgres memory mgmt.

    assert(strlen(cipher) < sizeof(this->cipherName));
    assert(keyLen <= sizeof(this->key));

    /* Save the configuration in our structure. */
    this->ctx = NULL;
    strcpy(this->cipherName, cipher);
    memcpy(this->key, key, keyLen);

    // Create the corresponding converter object.
    return converterNew(this, &aeadEncryptInterface);
}


/*
 * Start encrypting a stream of plaintext records.
 * It generates a file header with information to assist future decryption.
 */
size_t aeadDecryptBegin(AeadConverter *this, Byte *inBuf, size_t *inSize, Error *error)
{
    *error = errorOK;

    /* Set up the encryption cipher */
    aeadCipherSetup(this, error);
    this->encrypt = true;

    /* Point to the header for validation. Sanity check for size. */
    Byte *header = inBuf;
    Byte *bp = header;
    size_t headerSize = get16(&bp); /* TODO: Sanity check. */

    /* Point to the empty cipher text record. */
    Byte *cipherText = inBuf + headerSize;


    this->blockSize = get32(&bp);
    this->cipherSize = this->blockSize + get8(&bp);
    this->tagSize = get8(&bp);
    this->cipherName = getstr(&bp);  /* TODO: verify length first */
    putstr(&bp, this->cipherName); /* name of the cipher algorithm */
    putBytes(&bp, this->iv, this->ivSize);  /* The initialization vector */

    /* Get the size of the header, and insert total header size at the beginning of the header */
    size_t headerSize = bp - header;
    Byte *headerStart = header;
    put16(&headerStart, headerSize + this->cipherBlockSize + this->tagSize);

    /* Encrypt a zero length record with the header. Creates a single cipher block of padding. */
    Byte tag[EVP_MAX_MD_SIZE];
    size_t padSize = this->cipherBlockSize;
    aead_encrypt(this, NULL, 0, header, headerSize, bp, &padSize, tag, error);
    if (isError(*error))
        return 0;
    assert(padSize == this->cipherBlockSize);
    bp += padSize;

    /* Append the tag after the cipher text, just like any other record. */
    putBytes(&bp, tag, this->tagSize);

    /* We have produced our first record. bump the counter */
    this->blockNr++;

    /* Return the number of bytes generated */
    return bp - header;
}



/*
 * Encrypt the next block of plaintext.
 * The encrypted block will be larger due to
 *   1) Padding.   (allows block sizes which are not multiples of the cipher's block size.)
 *   2) MAC tag.
 */
void aeadDecryptProcess(AeadConverter *this, Byte *outBuf, size_t *outSize, Byte *inBuf, size_t *inSize, Error *error)
{
    /* Encrypt the input buffer. We don't have additional data to validate. */
    Byte tag[EVP_MAX_MD_SIZE];
    size_t bodySize = *outSize - this->tagSize;
    aead_encrypt(this, inBuf, *inSize, NULL, 0, outBuf, &bodySize, tag, error);
    if (isError(*error))
        return;

    /* Append the tag to the cipher text. */
    memcpy(outBuf + bodySize, tag, this->tagSize);

    /* Update the actual output size. */
    *outSize = bodySize + this->tagSize;

    /* We always end on a partial block, so keep track of this block size for next time. */
    this->blockNr++;
    this->previousSize = *inSize;
}


/**
 * Finalize the encryption stream.
 */
size_t aeadDecryptEnd(AeadConverter *this, Byte *outBuf, size_t outSize, Error *error)
{
    /* We always end on a partial block. If last block was full, then create an empty one */
    if (this->previousSize == this->blockSize)
        aeadEncryptProcess(this, outBuf, &outSize, NULL, 0, error);
    else
        outSize = 0;

    EVP_CIPHER_free(this->cipher);
    EVP_CIPHER_CTX_free(this->ctx);
    this->ctx = NULL;
    this->cipher = NULL;

    return outSize;
}



/**
 * Create a filter for encrypting/decrypting file streams using OpenSSL.
 * @param cipher - the OpenSSL name of the cipher
 * @param encrypt - 1 for encryption, 0 for decryption.
 * @param key - the key used for encryption/decryption.
 * @param keyLen - the length of the key.
 * @return - a filter ready to start encrypting or decrypting file data
 */
Filter *openAeadNew(char *cipherName, size_t blockSize, Byte *key, size_t keyLen, Filter *next)
{
    /* Create a filter which encrypts during writes and decrypts during reads. */
    Converter *encrypt = aeadEncryptNew(cipherName, blockSize, key, keyLen);
    Converter *decrypt = aeadDecryptNew(cipherName, blockSize, key, keyLen);
    return convertFilterNew(16*1024, encrypt, decrypt, next);
}




/*
 * Encrypt one record of plain text, generating one (slightly larger) record of cipher text.
 *  @param this - aaed converter
 *  @param plainText - the text to be encrypted.
 *  @param plainSize - size of the text to be encrypted
 *  @param header - text to be authenticated but not encrypted.
 *  @param headerSize - size of header. If 0, then header can be NULL.
 *  @param cipherText - the output encrypted text
 *  @param cipherSize - the size of the buffer on input, actual size on output.
 *  @param tag - the output MAC tag of size this->tagSize
 *  @param error - Keep track of errors.
 *  @return - the actual size of the encrypted ciphertext.
 */
size_t
aead_encrypt(AeadConverter *this,
             Byte *plainText, size_t plainSize,
             Byte *header, size_t headerSize,
             Byte *cipherText, size_t cipherSize,
             Byte *tag, Error *error)
{
    /* Reinitialize the encryption context to start a new record */
    EVP_CIPHER_CTX_reset(this->ctx);

    /* Generate nonce by XOR'ing the initialization vector with the sequence number */
    Byte nonce[EVP_MAX_IV_LENGTH];
    generateNonce(nonce, this->iv, this->ivSize, this->blockNr);

    /* Configure the key and initialization vector */
    if (!EVP_CipherInit_ex2(this->ctx, this->cipher, this->key, nonce, this->encrypt, NULL))
        return openSSLError(error);

    /* Include the header in the digest */
    if (headerSize > 0)
    {
        if (!EVP_CipherUpdate(this->ctx, NULL, 0, header, headerSize))
            return openSSLError(error);
    }

    /* Encrypt the body */
    int bodySize = 0;
    if (plainSize > 0)
    {
        bodySize = cipherSize;
        if (!EVP_CipherUpdate(this->ctx, cipherText, &bodySize, plainText, plainSize))
            return openSSLError(error);
    }

    /* Finalise the encryption. This can generate data, usually padding. */
    int finalSize = cipherSize - bodySize;
    if (!EVP_CipherFinal_ex(this->ctx, cipherText + bodySize, &finalSize))
        return openSSLError(error);

    /* Get the tag */
    if (!EVP_CIPHER_CTX_ctrl(this->ctx, EVP_CTRL_AEAD_GET_TAG, this->tagSize, tag))
        return openSSLError(error);

    /* Output ciphertext size combines the main part of the encryption and the finalization. */
    return bodySize + finalSize;
}



/*
 * Decrypt one record of ciphertext, generating one (slightly smaller?) record of plain text.
 * This routine implements a generic AEAD interface.
 *  @param this - aaed converter
 *  @param plainText - the text to be encrypted.
 *  @param plainSize - size of the text to be encrypted
 *  @param header - text to be authenticated but not encrypted.
 *  @param headerSize - size of header. If 0, then header can be NULL.
 *  @param cipherText - the output encrypted text
 *  @param cipherSize - the size of the buffer on input, actual size on output.
 *  @param tag - the output MAC tag of size this->tagSize
 *  @param error - Keep track of errors.
 */
size_t
aead_decrypt(AeadConverter *this,
             Byte *plainText, size_t plainSize,
             Byte *header, size_t headerSize,
             Byte *cipherText, size_t cipherSize,
             Byte *tag, Error *error)
{
    /* Reinitialize the encryption context to start a new record */
    EVP_CIPHER_CTX_reset(this->ctx);

    /* Generate nonce by XOR'ing the initialization vector with the sequence number */
    Byte nonce[EVP_MAX_IV_LENGTH];
    generateNonce(nonce, this->iv, this->ivSize, this->blockNr);

    /* Configure the key and initialization vector */
    if (!EVP_CipherInit_ex2(this->ctx, this->cipher, this->key, nonce, this->encrypt, NULL))
        return openSSLError(error);

    /* Set the MAC tag we need to match */
    if (!EVP_CIPHER_CTX_ctrl(this->ctx, EVP_CTRL_AEAD_SET_TAG, this->tagSize, tag))
        return openSSLError(error);

    /* Include the header in the digest */
    if (headerSize > 0)
    {
        if (!EVP_CipherUpdate(this->ctx, NULL, 0, header, headerSize))
            return openSSLError(error);
    }

    /* Decrypt the body */
    int bodySize = 0;
    if (plainSize > 0)
    {
        bodySize = cipherSize;
        if (!EVP_CipherUpdate(this->ctx, cipherText, &bodySize, plainText, plainSize))
            return openSSLError(error);
    }

    /* Finalise the decryption. This can, but probably won't, generate plaintext. */
    int finalSize = cipherSize - bodySize;
    if (!EVP_CipherFinal_ex(this->ctx, cipherText + bodySize, &finalSize))
        return openSSLError(error);

    /* Output plaintext size combines the main part of the encryption and the finalization. */
    return bodySize + finalSize;
}


/*
 * Create a "nonce" (number used once) by XOR'ing a sequence number
 * with an initialization vector (IV).
 *
 * As described in RFC 8446  for TLS 1.3,
 *  - extend the sequence number with zeros to match the IV size.
 *  - process the sequence number bytes in network (big endian) order.
 *  - XOR the IV and sequence bytes to create the nonce.
 *  (Actual implementation differs.)
 */
void generateNonce(Byte *nonce, Byte *iv, size_t ivSize, size_t seqNr)
{
    /* Starting at the right, create the nonce moving left one byte at a time */
    Byte *nonceP = nonce + ivSize;
    Byte *ivP = iv + ivSize;
    while (nonceP > nonce)
    {
        /* Move the pointers leftward in unison */
        nonceP--; ivP--;

        /* Create the nonce byte by XOR'ing sequence number with IV. */
        *nonceP = (Byte)seqNr ^ *ivP;

        /* get next higher byte of the sequence number, rolling off to zero. */
        seqNr = seqNr >> 8;
    }
}

/*
 * Convenience function to build an SSL error and return zero.
 */
static size_t openSSLError(Error *error)
{
    if (errorIsOK(*error))
        *error = (Error){.code=errorCodeFilter, .msg=ERR_error_string(ERR_get_error(), NULL)};
    return 0;
}
