/*
 * Currently, we pad the last cipherblock in a record.
 * Alternative is to pad the last record, and don't pad the cipherblocks.
 * Preferable, esp if the MACs and header are kept separately, since it maintains block alignmant.
 *
 * TODO: add a file header with cipher config information.
 * TODO: request block size 1 to support header.
 */
#include <stdlib.h>
#include <assert.h>
#include <openssl/opensslv.h>
#include <openssl/ssl.h>

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include "encrypt/libcrypto/aead.h"
#include "common/filter.h"
#include "common/passThrough.h"

/* Forward references */
static const Error errorBadKeyLen = (Error){.code=errorCodeFilter, .msg="Unexpected Key or IV length."};
static const Error errorBadDecryption = (Error) {.code=errorCodeFilter, .msg="Unable to decrypt current buffer"};
static size_t openSSLError(Error *error);
void generateNonce(Byte *nonce, Byte *iv, size_t ivSize, size_t seqNr);
size_t aead_encrypt(AeadFilter *this, Byte *plainText, size_t plainSize, Byte *header,
                  size_t headerSize, Byte *cipherText, size_t cipherSize,Byte *tag, Error *error);
size_t aead_decrypt(AeadFilter *this, Byte *plainText, size_t plainSize, Byte *header,
                  size_t headerSize, Byte *cipherText, size_t cipherSize, Byte *tag, Error *error);
void aeadCipherSetup(AeadFilter *this, char *cipherName, Error *error);

/**
 * Converter structure for encrypting and decrypting TLS Records.
 */
#define MAX_CIPHER_NAME 64
struct AeadFilter
{
    Filter filter;

    /* Configuration */
    char cipherName[MAX_CIPHER_NAME];   /* The name of the cipher */
    Byte key[EVP_MAX_KEY_LENGTH];       /* The key for encrypting/decrypting */
    size_t plainBlockSize;              /* The plaintext block size */
    Byte iv[EVP_MAX_IV_LENGTH];         /* The initialization vector for the sequence of records. */
    size_t headerSize;

    /* Cipher State (based on the encryption algorithm) */
    size_t keySize;              /* The size of the key in bytes */
    size_t ivSize;               /* Size of the initialization vector, typically same as blockSize */
    size_t cipherBlockSize;      /* Size of the cipher block. (typically 16 bytes for AES) */
    size_t tagSize;              /* Size of the MAC tag to authenticate the encrypted record. */
    size_t padding;              /* The amount of padding added to the default block size. */
    EVP_CIPHER *cipher;          /* The libcrypto cipher structure */
    EVP_CIPHER_CTX *ctx;         /* Libcrypto context. */

    /* Our state */
    size_t blockNr;              /* The record sequence number, starting at 0 and incrementing. */
    size_t blockSize;            /* The size of the encrypted blocks */
    size_t previousSize;
    Byte *buf;                   /* Buffer to hold the current encrypted block */
    bool eof;
};


/**
 * Open a Filtered Stream.
 * @param path - the path or file name.
 * @param mode - the file mode, say O_RDONLY or O_CREATE.
 * @param perm - if creating a file, the permissions.
 * @return - Error status.
 */
Error aeadFilterOpen(AeadFilter *this, char *path, int mode, int perm)
{
    this->eof = false;

    /* Go ahead and open the downstream file. */
    Error error = passThroughOpen(this, path, mode, perm);

    /* Position at the beginning */
    passThroughSeek(this, 0, &error);
    this->blockNr = 0;

    /* Read configuration from the file header */
    /* Later - for now just set the parameters. */
    memcpy(this->iv, "1234567890123456", 16);
    this->plainBlockSize = 1024;
    this->headerSize = 0;

    /* Configure the cipher parameters. */
    aeadCipherSetup(this, "AES_256_CBC", &error);

    return error;
}

/**
 * Read a block of encrypted data into our internal buffer, placing plaintext into the caller's buffer.
 */
size_t aeadFilterRead(AeadFilter *this, Byte *buf, size_t size, Error *error)
{
    /* First, check if we have encountered EOF on a previous read. */
    if (this->eof)
        return setError(error, errorEOF);

    /* Read a block of downstream encrypted text into our buffer. */
    size_t actual = passThroughReadAll(this, this->buf, this->blockSize, error);
    if (isError(*error))
        return 0;

    /* Extract the tag from the end of our buffer. */
    Byte tag[EVP_MAX_MD_SIZE];
    size_t cipherTextSize = actual - this->tagSize;
    memcpy(tag, this->buf + cipherTextSize, this->tagSize);

    /* Decrypt the ciphertext from our buffer. */
    size_t plainSize = aead_decrypt(this, buf, size, NULL, 0, this->buf, cipherTextSize, tag, error);
    if (isError(*error))
        return 0;

    /* We end the file on a partial block, so keep track of this block size for next time. */
    this->previousSize = plainSize;

    /* Return the number of plain text bytes read. */
    this->blockNr++;
    return plainSize;
}

/**
 * Encrypt data into our internal buffer and write to the output file.
 *   @param buf - data to be converted.
 *   @param size - number of bytes to be converted.
 *   @param error - error status, both input and output.
 *   @returns - number of bytes actually used.
 */
size_t aeadFilterWrite(AeadFilter *this, Byte *buf, size_t size, Error *error)
{
    /* Encrypt one block of data into our buffer */
    Byte tag[EVP_MAX_MD_SIZE];
    size_t plainSize = sizeMin(size, this->plainBlockSize);
    size_t cipherSize = aead_encrypt(this, buf, plainSize, NULL, 0, this->buf, this->blockSize - this->tagSize, tag, error);

    /* Append the tag to the encrypted data */
    memcpy(this->buf+cipherSize, tag, this->tagSize);
    cipherSize += this->tagSize;

    /* Write the encrypted block out */
    passThroughWriteAll(this, this->buf, cipherSize, error);

    /* We have just advanced to the next block. */
    this->blockNr++;

    return plainSize;
}

/**
 * Close this conversion filter, releasing resources.
 * @param error - error status
 */
void aeadFilterClose(AeadFilter *this, Error *error)
{
    if (isError(*error)) return;

    /* Get the size of our downstream file. TODO: only when writing! */
    size_t fileSize = passThroughSeek(this, FILE_END_POSITION, error);

    /* If downstream file consists of full size blocks, add a zero length partial block */
    if (fileSize % this->blockSize == 0)
    {
        this->blockNr = fileSize / this->blockSize;
        aeadFilterWrite(this, this->buf, 0, error);
    }

    /* Notify the downstream file it must close as well. */
    passThroughClose(this, error);

    free(this->buf);
    this->buf = NULL;
}


/*
 * Seek to the specified block boundary and return the new position.
 * Exception for seeking to the end of file, where the new position
 * is at the beginning of the last partial block returning the file size.
 */
pos_t aeadFilterSeek(AeadFilter *this, pos_t position, Error *error)
{
    pos_t offset = 0;

    /* If seeking to the end, make note of the block and offset */
    if (position == FILE_END_POSITION)
    {
        /* Get the file size and set position to the last partial block. */
        size_t fileSize = passThroughSeek(this, position, error);
        position = fileSize / this->blockSize * this->plainBlockSize;

        /* If the last block is partial, subtract the empty size from the offset. */
        /*  Note we are not precise, except when there is no partial block, */
        /*  or if the partial block is empty. */
        offset = fileSize % this->blockSize;
        if (offset >= this->cipherBlockSize + this->tagSize)
            offset -= this->cipherBlockSize + this->tagSize;
    }

    /* Verify we are seeking to a block boundary */
    if (position % this->plainBlockSize != 0)
        return filterError(error, "Must seek to a block boundary");

    /* Set the new block number and go there in the downstream file. */
    this->blockNr = position / this->plainBlockSize;
    passThroughSeek(this, this->blockNr * this->blockSize, error);

    /* Return the new position, adding in offset if requesting file size. */
    return position + offset;
}


size_t aeadFilterBlockSize(AeadFilter *this, size_t prevSize, Error *error)
{
    /* Our plaintext block size is fixed. Calculate the corresponnding encrypted size. */
    this->blockSize = this->plainBlockSize + this->padding + this->tagSize;

    /* Negotiate with the next stage. Our encryption block must be a multiple of the next stage'. */
    size_t nextSize = passThroughBlockSize(this, this->blockSize, error);
    if (this->blockSize % nextSize != 0)
        return filterError(error, "AEAD Encryption blocksize is not aligned.");

    /* Allocate a buffer to hold a block of encrypted data. */
    this->buf = malloc(this->blockSize);  /* TODO: memory mgmt */

    /* Tell the previous stage they must accommodate our plaintext block size. */
    return this->plainBlockSize;
}

/**
 * Abstract interface for the encryption filter.
 */
FilterInterface aeadFilterInterface = {
        .fnOpen = (FilterOpen) aeadFilterOpen,
        .fnRead = (FilterRead) aeadFilterRead,
        .fnWrite = (FilterWrite)aeadFilterWrite,
        .fnClose = (FilterClose) aeadFilterClose,
        .fnSeek = (FilterSeek) aeadFilterSeek,
        .fnBlockSize = (FilterBlockSize) aeadFilterBlockSize,
};

Filter *aeadFilterNew(char *cipherName, size_t blockSize, Byte *key, size_t keySize, Filter *next)
{
    AeadFilter *this = malloc(sizeof(AeadFilter));

    /* Save the key without overwriting memory. We'll verify the key length later. */
    assert(keySize <= sizeof(this->key));
    this->keySize = keySize;
    memcpy(this->key, key, keySize);

    /* Save defaults for creating a new file. Otherwise, we'll read them from file header. */
    strlcpy(this->cipherName, cipherName, sizeof(this->cipherName));
    this->plainBlockSize = blockSize;

    return filterInit(this, &aeadFilterInterface, next);
}

/**
 * Helper function to get the cipher details.
 */
void aeadCipherSetup(AeadFilter *this, char *cipherName, Error *error)
{
    /* Create an OpenSSL cipher context. */
    this->ctx = EVP_CIPHER_CTX_new();

    /* Save the cipher name. The name must be an exact match to a libcrypto name. */
    strlcpy(this->cipherName, cipherName, sizeof(this->cipherName));

    /* Lookup cipher by name. */
    //this->cipher = EVP_CIPHER_fetch(NULL, this->cipherName, NULL);
    this->cipher = EVP_aes_256_gcm(); /* TODO: Temporary */
    if (this->cipher == NULL)
        return (void) filterError(error, "Encryption problem - cipher name not recognized");

    /* Get the properties of the selected cipher */
    this->ivSize = EVP_CIPHER_iv_length(this->cipher);
    this->keySize = EVP_CIPHER_key_length(this->cipher);
    this->cipherBlockSize = EVP_CIPHER_block_size(this->cipher);
    this->padding = (this->cipherBlockSize == 1)
        ? 0
        : this->cipherBlockSize - (this->plainBlockSize % this->cipherBlockSize);
    this->tagSize = 16;  /* TODO: How do we query for it? */
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
aead_encrypt(AeadFilter *this,
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

    /* Configure the cipher with key and initialization vector */
    if (!EVP_CipherInit_ex2(this->ctx, this->cipher, this->key, nonce, 1, NULL))
        return openSSLError(error);

    /* Include the header in the digest */
    if (headerSize > 0)
    {
        if (!EVP_CipherUpdate(this->ctx, NULL, 0, header, (int)headerSize))
            return openSSLError(error);
    }

    /* Encrypt the plaintext. There are two pieces produced by update and final. */
    int cipherUpdateSize = 0;
    if (plainSize > 0)
    {
        cipherUpdateSize = (int)cipherSize;
        if (!EVP_CipherUpdate(this->ctx, cipherText, &cipherUpdateSize, plainText, (int)plainSize))
            return openSSLError(error);
    }

    /* Finalise the encryption. This can generate data, usually padding, even if there is no plain text. */
    int cipherFinalSize = (int)cipherSize - cipherUpdateSize;
    if (!EVP_CipherFinal_ex(this->ctx, cipherText + cipherUpdateSize, &cipherFinalSize))
        return openSSLError(error);

    /* Get the tag  */
    if (!EVP_CIPHER_CTX_ctrl(this->ctx, EVP_CTRL_AEAD_GET_TAG, (int)this->tagSize, tag))
        return openSSLError(error);

    /* Output ciphertext size combines the main part of the encryption and the finalization. */
    return cipherUpdateSize + cipherFinalSize;
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
aead_decrypt(AeadFilter *this,
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

    /* Configure the cipher with key and initialization vector */
    if (!EVP_CipherInit_ex2(this->ctx, this->cipher, this->key, nonce, 0, NULL))
        return openSSLError(error);

    /* Set the MAC tag we need to match */
    if (!EVP_CIPHER_CTX_ctrl(this->ctx, EVP_CTRL_AEAD_SET_TAG, (int)this->tagSize, tag))
        return openSSLError(error);

    /* Include the header in the digest */
    if (headerSize > 0)
    {
        if (!EVP_CipherUpdate(this->ctx, NULL, 0, header, (int)headerSize))
            return openSSLError(error);
    }

    /* Decrypt the body. We have two pieces: update and final. */
    int plainUpdateSize = 0;
    if (cipherSize > 0)
    {
        plainUpdateSize = (int)plainSize;
        if (!EVP_CipherUpdate(this->ctx, plainText, &plainUpdateSize, cipherText, (int)cipherSize))
            return openSSLError(error);
    }

    /* Finalise the decryption. This can, but probably won't, generate plaintext. */
    /*  NOTE: CipherFinal returns 0  (error) after small (eg 3 byte) decryptions. Check get_error() as a workaround */
    int plainFinalSize = (int)plainSize - plainUpdateSize;
    if (!EVP_CipherFinal_ex(this->ctx, plainText + plainUpdateSize, &plainFinalSize) && ERR_get_error() != 0)
        return openSSLError(error);

    /* Output plaintext size combines the update part of the encryption and the finalization. */
    return plainUpdateSize + plainFinalSize;
}


/*
 * Create a "nonce" (number used once) by XOR'ing a sequence number
 * with an initialization vector (IV).
 *
 * As described in RFC 8446  for TLS 1.3,
 *  - extend the sequence number with zeros to match the IV size.
 *  - process the sequence number in network (big endian) order.
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
