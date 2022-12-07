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
#include <fcntl.h>
#include <openssl/opensslv.h>
#include <openssl/ssl.h>

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include "encrypt/libcrypto/aead.h"
#include "common/filter.h"
#include "common/passThrough.h"
#include "common/packed.h"

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
void aeadConfigure(AeadFilter *this, int oflag, Error *error);
void aeadHeaderRead(AeadFilter *this, Error *error);
void aeadHeaderWrite(AeadFilter *this, Error *error);
size_t paddingSize(AeadFilter *this, size_t recordSize);

/**
 * Converter structure for encrypting and decrypting TLS Records.
 */
#define MAX_CIPHER_NAME 64
#define MAX_AEAD_HEADER_SIZE 1024
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
    bool hasPadding;              /* The amount of padding added to the default block size. */
    const EVP_CIPHER *cipher;          /* The libcrypto cipher structure */
    EVP_CIPHER_CTX *ctx;         /* Libcrypto context. */

    /* Our state */
    size_t blockNr;              /* The record sequence number, starting at 0 and incrementing. */
    size_t blockSize;            /* The size of the encrypted blocks */
    size_t previousSize;
    Byte *buf;                   /* Buffer to hold the current encrypted block */
    bool eof;
};


/**
 * Open an encrypted file.
 * @param path - the path or file name.
 * @param oflag - the open flags, say O_RDONLY or O_CREATE.
 * @param mode - if creating a file, the permissions.
 * @return - Error status.
 */
Error aeadFilterOpen(AeadFilter *this, char *path, int oflag, int mode)
{
    this->eof = false;

    /* Go ahead and open the downstream file. */
    Error error = passThroughOpen(this, path, oflag, mode);

    /* Note we are positioned at the beginning */
    this->blockNr = 0;

    /* Configure, we will be positioned at end of header. */
    aeadConfigure(this, oflag, &error);

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
    if (isError(*error))
        return 0;

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
 * Close this encryption filter, releasing resources.
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
 * Seek to the specified block boundary and return the new plainPosition.
 * Exception for seeking to the end of file, where the new plainPosition
 * is at the beginning of the last partial block returning the file size.
 */
pos_t aeadFilterSeek(AeadFilter *this, pos_t plainPosition, Error *error)
{
    pos_t offset = 0;

    /* If seeking to the end, make note of the block and offset */
    if (plainPosition == FILE_END_POSITION)
    {
        /* Get the file size and set plainPosition to the last partial block. */
        size_t fileSize = passThroughSeek(this, FILE_END_POSITION, error);
        plainPosition = (fileSize - this->headerSize) / this->blockSize * this->plainBlockSize;

        /* If the last block is partial, subtract the empty size from the offset. */
        /*  Note we are not precise, except when there is no partial block, */
        /*  or if the partial block is empty. */
        offset = (fileSize - this->headerSize) % this->blockSize;
        if (offset >= this->cipherBlockSize + this->tagSize)
            offset -= this->cipherBlockSize + this->tagSize;
    }

    /* Verify we are seeking to a block boundary */
    if (plainPosition % this->plainBlockSize != 0)
        return filterError(error, "Must seek to a block boundary");

    /* Set the new block number and go there in the downstream file. */
    this->blockNr = plainPosition / this->plainBlockSize;
    pos_t cipherPosition = this->headerSize + this->blockNr * this->blockSize;
    passThroughSeek(this, cipherPosition, error);

    /* Return the new plainPosition, adding in offset if requesting file size. */
    return plainPosition + offset;
}


size_t aeadFilterBlockSize(AeadFilter *this, size_t prevSize, Error *error)
{
    /* Our plaintext block size is fixed. Calculate the corresponnding encrypted size. */
    this->blockSize = this->plainBlockSize + paddingSize(this, this->plainBlockSize) + this->tagSize;

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



void aeadConfigure(AeadFilter *this, int oflag, Error *error)
{
    /* Is the file readable/writable? */
    bool writable = (oflag & O_ACCMODE) != O_RDONLY;
    bool readable = (oflag & O_ACCMODE) != O_WRONLY;
    bool append = (oflag & O_APPEND) != 0;

    /* We do NOT support append mode. Must precede with buffering to support O_APPEND. */
    if (append)
        return (void) filterError(error, "Can't append to encrypted file - use buffering");

    /* Find the size of the file */
    pos_t fileSize = passThroughSeek(this, FILE_END_POSITION, error);
    passThroughSeek(this, 0, error);  /* Go back to start of file */
    if (isError(*error))
        return;

    /* if file has data, then read the existing header */
    if (fileSize > 0)
        aeadHeaderRead(this, error);
    else if (writable)
        aeadHeaderWrite(this, error);
    else
        filterError(error, "AEAD encryption can't read header");
}


void aeadHeaderRead(AeadFilter *this, Error *error)
{
    if (isError(*error))
        return;

    /* Read the header */
    Byte header[MAX_AEAD_HEADER_SIZE];
    size_t headerSize = passThroughReadSized(this, header, sizeof(header), error);

    /* Extract the various fields from the header, ensuring safe memory references */
    Byte *bp = header;
    Byte *end = header + headerSize;

    /* Get the plain text record size for this encrypted file. */
    this->plainBlockSize = unpack4(&bp, end);
    if (this->plainBlockSize > MAX_RECORD_SIZE)
        return (void) filterError(error, "AEAD record size in header is > 16MB");

    /* Get the cipher name */
    size_t nameSize = unpack1(&bp, end);
    if (nameSize > sizeof(this->cipherName) - 1)  /* allow for null termination */
        return (void) filterError(error, "Cipher name in header is too large");
    unpackBytes(&bp, end, (Byte *)this->cipherName, nameSize);
    this->cipherName[nameSize] = '\0';

    /* Get the initialization vector */
    this->ivSize = unpack1(&bp, end);
    if (this->ivSize > sizeof(this->iv))
        return (void) filterError(error, "Initialization vector is too large");
    unpackBytes(&bp, end, this->iv, this->ivSize);

    /* Get the empty cipher text block */
    Byte emptyBlock[EVP_MAX_BLOCK_LENGTH];
    size_t emptySize = unpack1(&bp, end);
    if (emptySize > sizeof(emptyBlock))
        return (void) filterError(error, "Empty cipher block in header is too large");
    unpackBytes(&bp, end, emptyBlock, emptySize);

    /* Get the MAC tag */
    Byte tag[EVP_MAX_MD_SIZE];
    this->tagSize = unpack1(&bp, end);
    if (this->tagSize > sizeof(tag))
        return (void) filterError(error, "Authentication tag is too large");
    unpackBytes(&bp, end, tag, this->tagSize);

    /* Verify we haven't overflowed. Ideally, we should have bp == end */
    if (bp > end)
        return (void) filterError(error, "Invalid AEAD header in file");

    /* Lookup the cipher and its parameters. */
    aeadCipherSetup(this, this->cipherName, error);
    if (isError(*error))
        return;

    /* Validate the header. The tag and empty block are separate, so don't include them. */
    Byte plainEmpty[0];
    headerSize = headerSize - this->tagSize - 1 - emptySize - 1;
    size_t plainSize = aead_decrypt(this, plainEmpty, sizeof(plainEmpty), header, headerSize, emptyBlock, emptySize, tag, error);

    /* set the ciphertext block size */
    this->blockSize = this->plainBlockSize + paddingSize(this, this->blockSize) + this->tagSize;
}


void aeadHeaderWrite(AeadFilter *this, Error *error)
{
    /* Configure the cipher parameters. */
    aeadCipherSetup(this, this->cipherName, error);
    if (isError(*error))
        return;

    /* Declare a local buffer to hold the header we're creating */
    Byte header[MAX_AEAD_HEADER_SIZE];
    Byte *bp = header;
    Byte *end = header + sizeof(header);

    /* Plaintext record size for this file. */
    pack4(&bp, end, this->plainBlockSize);

    /* Cipher name */
    pack1(&bp, end, strlen(this->cipherName));
    packBytes(&bp, end, (Byte *)this->cipherName, strlen(this->cipherName));

    /* Initialization vector */
    pack1(&bp, end, this->ivSize);
    packBytes(&bp, end, this->iv, this->ivSize);

    /* Verify we haven't overflowed our buffer. */
    if (bp > end)
        return (void)filterError(error, "Trying to write a header which is too large");

    /* Encrypt an empty plaintext block and authenticate the header. */
    Byte emptyCiphertext[EVP_MAX_BLOCK_LENGTH];
    Byte emptyPlaintext[0];
    Byte tag[EVP_MAX_MD_SIZE];
    size_t emptyCipherSize = aead_encrypt(this, emptyPlaintext, 0, header, end-header,
                                          emptyCiphertext, sizeof(emptyCiphertext), tag, error);
    if (emptyCipherSize != paddingSize(this, 0) || emptyCipherSize > 256)
        return (void)filterError(error, "Size of empty block padding was miscalculated");

    /* Add the empty block and tag to the header */
    pack1(&bp, end, emptyCipherSize);
    packBytes(&bp, end, emptyCiphertext, emptyCipherSize);
    pack1(&bp, end, this->tagSize);
    packBytes(&bp, end, tag, this->tagSize);

    /* Verify we haven't overflowed the header */
    if (bp > end)
        return (void)filterError(error, "Encryption file header was too large.");

    /* Write the header to the output file */
    passThroughWriteSized(this, header, bp-header, error);

    /* That was block 0. Now we are on the next block. */
    this->blockNr++;
}


/**
 * Helper function to get the cipher details.
 */
void aeadCipherSetup(AeadFilter *this, char *cipherName, Error *error)
{
    /* Create an OpenSSL cipher context. */
    this->ctx = EVP_CIPHER_CTX_new();

    /* Save the cipher name. The name must be an exact match to a libcrypto name. */
    /* TODO KLUDGE: The cipher name is already in this->cipherName. Don't copy if already there ... FIX IT!  */
    if (this->cipherName != cipherName) /* comparing pointers */
        strlcpy(this->cipherName, cipherName, sizeof(this->cipherName));

    /* Lookup cipher by name. */
    this->cipher = EVP_CIPHER_fetch(NULL, this->cipherName, NULL);
    this->cipher = EVP_aes_256_gcm(); /* TODO: Temporary */
    if (this->cipher == NULL)
        return (void)filterError(error, "Encryption problem - cipher name not recognized");

    /* Get the properties of the selected cipher */
    this->ivSize = EVP_CIPHER_iv_length(this->cipher);
    this->keySize = EVP_CIPHER_key_length(this->cipher);
    this->cipherBlockSize = EVP_CIPHER_block_size(this->cipher);
    this->hasPadding = (this->cipherBlockSize != 1);
    this->tagSize = 16;  /* TODO: How do we query for it? */
}


/*
 * Calculate how much padding is added when encrypting a record.
 */
size_t paddingSize(AeadFilter *this, size_t recordSize)
{
    return (this->hasPadding)
       ? this->cipherBlockSize - (this->plainBlockSize % this->cipherBlockSize)
       : 0;
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

    /* Configure the cipher with the key and nonce */
    if (!EVP_CipherInit_ex2(this->ctx, this->cipher, this->key, nonce, 1, NULL))
        return openSSLError(error);

    /* Include the header, if any, in the digest */
    if (headerSize > 0)
    {
        int zero = 0;
        if (!EVP_CipherUpdate(this->ctx, NULL, &zero, header, (int)headerSize))
            return openSSLError(error);
    }

    /* Encrypt the plaintext if any. */
    int cipherUpdateSize = 0;
    if (plainSize > 0)
    {
        cipherUpdateSize = (int)cipherSize;
        if (!EVP_CipherUpdate(this->ctx, cipherText, &cipherUpdateSize, plainText, (int)plainSize))
            return openSSLError(error);
    }

    /* Finalise the plaintext encryption. This can generate data, usually padding, even if there is no plain text. */
    int cipherFinalSize = (int)cipherSize - cipherUpdateSize;
    if (!EVP_CipherFinal_ex(this->ctx, cipherText + cipherUpdateSize, &cipherFinalSize))
        return openSSLError(error);

    /* Get the authentication tag  */
    if (!EVP_CIPHER_CTX_ctrl(this->ctx, EVP_CTRL_AEAD_GET_TAG, (int)this->tagSize, tag))
        return openSSLError(error);

    /* Output size combines both the encyption (update) and the finalization. */
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

    /* Include the header, if any, in the digest */
    if (headerSize > 0)
    {
        int zero = 0;
        if (!EVP_CipherUpdate(this->ctx, NULL, &zero, header, (int)headerSize))
            return openSSLError(error);
    }

    /* Decrypt the body if any. We have two pieces: update and final. */
    int plainUpdateSize = 0;
    if (cipherSize > 0)
    {
        plainUpdateSize = (int)plainSize;
        if (!EVP_CipherUpdate(this->ctx, plainText, &plainUpdateSize, cipherText, (int)cipherSize))
            return openSSLError(error);
    }

    /* Finalise the decryption. This can, but probably won't, generate plaintext. */
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
