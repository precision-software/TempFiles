/*
 *
 */
//#define DEBUG
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <openssl/opensslv.h>
#include <openssl/ssl.h>

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include "common/debug.h"

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
void aeadConfigure(AeadFilter *this, Error *error);
void aeadHeaderRead(AeadFilter *this, Error *error);
void aeadHeaderWrite(AeadFilter *this, Error *error);
size_t paddingSize(AeadFilter *this, size_t recordSize);
pos_t aeadFilterSeek(AeadFilter *this, pos_t position, Error *error);

/**
 * Converter structure for encrypting and decrypting TLS Records.
 */
#define MAX_CIPHER_NAME 64
#define MAX_AEAD_HEADER_SIZE 1024
#define HEADER_SEQUENCE_NUMBER ((size_t)-1)
struct AeadFilter
{
    Filter filter;

    /* Configuration */
    Byte key[EVP_MAX_KEY_LENGTH];       /* The key for encrypting/decrypting */
    char cipherName[MAX_CIPHER_NAME];   /* The name of the cipher, if encrypting a new file. */
    size_t plainSize;             /* The plaintext record size, if encrypting a new file */

    /* Cipher State (based on the encryption algorithm) */
    size_t keySize;              /* The size of the key in bytes */
    size_t ivSize;               /* Size of the initialization vector, typically same as cipherSize */
    size_t blockSize;            /* Size of the cipher block. (typically 16 bytes for AES) */
    size_t tagSize;              /* Size of the MAC tag to authenticate the encrypted record. */
    bool hasPadding;             /* Whether cipher block padding is added to the encrypted records */
    Byte iv[EVP_MAX_IV_LENGTH];  /* The initialization vector for the sequence of records. */
    EVP_CIPHER *cipher;          /* The libcrypto cipher structure */
    EVP_CIPHER_CTX *ctx;         /* Libcrypto context. */

    /* Our state */
    size_t headerSize;            /* Size of the header we read/wrote to the encrypted file */
    size_t recordNr;              /* The record sequence number, starting at 0 and incrementing. */
    size_t cipherSize;            /* The size of the encrypted records */
    Byte *cipherBuf;                    /* Buffer to hold the current encrypted block */
    bool readable;
    bool writable;

    /* Our plaintext positions used to decide if we need to add zero length record at end. */
    pos_t fileSize;               /* (MAX if unknown, 0 if truncated, actual otherwise) */
    pos_t position;
    pos_t maxReadPosition;        /* Biggest position after reading */
    pos_t maxWritePosition;       /* Biggest position after writing */

    Byte *plainBuf;                /* A buffer to temporarily hold a decrypted record */
};


/**
 * Open an encrypted file.
 * @param path - the path or file name.
 * @param oflag - the open flags, say O_RDONLY or O_CREATE.
 * @param mode - if creating a file, the permissions.
 * @return - Error status.
 */
Filter *aeadFilterOpen(AeadFilter *pipe, char *path, int oflags, int mode, Error *error)
{
    /* We do NOT support append mode directly. Use Buffering if O_APPEND is needed. */
    if ((oflags & O_APPEND) != 0)
        return (filterError(error, "Can't directly append to encrypted file - use buffering"), NULL);

    /* We need to read header, even if otherwise write only */
    if ( (oflags & O_ACCMODE) == O_WRONLY)
        oflags = (oflags & ~O_ACCMODE) | O_RDWR;

    /* Go ahead and open the downstream file. */
    Filter *next = passThroughOpen(pipe, path, oflags, mode, error);

    /* Clone ourselves, attaching to the downstream clone */
    AeadFilter *this = aeadFilterNew(pipe->cipherName, pipe->plainSize, pipe->key, pipe->keySize, next);

    /* Is the file readable/writable? */
    this->writable = (oflags & O_ACCMODE) != O_RDONLY;
    this->readable = (oflags & O_ACCMODE) != O_WRONLY;

    /* Note we are positioned at the first record and we've done no I/O so far. */
    this->recordNr = 0;
    this->maxReadPosition = 0;
    this->maxWritePosition = 0;
    this->fileSize = (oflags & O_TRUNC)? 0 : FILE_END_POSITION;
    this->position = 0;
    this->cipherBuf = NULL;
    this->plainBuf = NULL;

    return (Filter *)this;
}


/**
 * Read a block of encrypted data into our internal buffer, placing plaintext into the caller's buffer.
 */
size_t aeadFilterRead(AeadFilter *this, Byte *buf, size_t size, Error *error)
{
    debug("aeadFilterRead: size=%zu  position=%llu maxWrite=%llu maxRead=%llu fileSize=%lld\n",
          size, this->position, this->maxWritePosition, this->maxReadPosition, (off_t)this->fileSize);
    /* Read a block of downstream encrypted text into our buffer. */
    size_t actual = passThroughReadAll(this, this->cipherBuf, this->cipherSize, error);
    if (isError(*error))
        return 0;

    /* Extract the tag from the end of our buffer. */
    Byte tag[EVP_MAX_MD_SIZE];
    size_t cipherTextSize = actual - this->tagSize;
    memcpy(tag, this->cipherBuf + cipherTextSize, this->tagSize);

    /* Decrypt the ciphertext from our buffer. */
    size_t plainSize = aead_decrypt(this, buf, size, NULL, 0, this->cipherBuf, cipherTextSize, tag, error);
    if (isError(*error))
        return 0;

    /* Track our position for EOF handling */
    this->position += plainSize;
    this->maxReadPosition = sizeMax(this->maxReadPosition, this->position);
    if (plainSize < this->plainSize)
        this->fileSize = this->position;

    /* If partial record, probe to make sure the file is really EOF. */
    if (plainSize < this->plainSize)
    {
        passThroughRead(this, this->cipherBuf, 1, error);
        if (!errorIsEOF(*error))
            return filterError(error, "Encrypted file has extra data appended.");
        *error = errorOK;
    }

    /* If the partial record was empty, then we're at EOF now */
    if (plainSize == 0)
        return setError(error, errorEOF);

    /* Track our position for EOF handling */
    this->maxReadPosition = sizeMax(this->maxReadPosition, this->recordNr * this->plainSize + plainSize);

    /* Bump the record sequence number if full block read */
    if (plainSize == this->plainSize)
        this->recordNr++;

    /* Return the number of plaintext bytes read. */
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
    debug("aeadFilterWrite: size=%zu  position=%llu maxWrite=%llu maxRead=%llu fileSize=%lld\n",
          size, this->position, this->maxWritePosition, this->maxReadPosition, (off_t)this->fileSize);
    if (isError(*error))
        return 0;

    /* Encrypt one record of data into our buffer */
    Byte tag[EVP_MAX_MD_SIZE];
    size_t plainSize = sizeMin(size, this->plainSize);
    size_t cipherSize = aead_encrypt(this, buf, plainSize, NULL, 0, this->cipherBuf, this->cipherSize - this->tagSize, tag, error);

    /* Append the tag to the encrypted data */
    memcpy(this->cipherBuf + cipherSize, tag, this->tagSize);
    cipherSize += this->tagSize;

    /* Write the encrypted block out */
    passThroughWriteAll(this, this->cipherBuf, cipherSize, error);

    /* Track our position for EOF handling */
    this->position += plainSize;
    this->maxWritePosition = sizeMax(this->maxWritePosition, this->position);

    /* Partial write indicates EOF - is it true? */
    /* TODO: verify partial write is at end of file */

    /* We have just advanced to the next block. */
    this->recordNr++;

    return plainSize;
}

/**
 * Close this encryption filter, releasing resources.
 * @param error - error status
 */
void aeadFilterClose(AeadFilter *this, Error *error)
{
    debug("aeadFilterClose: position=%llu maxWrite=%llu maxRead=%llu fileSize=%lld\n",
          this->position, this->maxWritePosition, this->maxReadPosition, (off_t)this->fileSize);
    /* Do we need to read or write a final empty record? */
    /* CASE: NO. file is bigger then max write (because we read it) */
    if (this->maxReadPosition > this->maxWritePosition)
        ;

    /* CASE: NO. the biggest write was a partial block. No need to add empty block */
    else if (this->maxWritePosition % this->plainSize != 0)
        ;

    /* CASE: NO. We know the file size and it is bigger than last write */
    else if (this->fileSize != FILE_END_POSITION && this->fileSize > this->maxWritePosition)
        ;

    else
    {
        /* CASE: NO. downstream file size is bigger than our highest write. (PADDING PROBLEMS!)*/
        size_t actualSize = passThroughSeek(this, FILE_END_POSITION, error);
        size_t expectedSize = this->maxWritePosition / this->plainSize * this->cipherSize + this->headerSize;
        if (actualSize > expectedSize)
            ;

        /* OTHERWISE: YES. Add an empty block to the end. (Final record is full, since partials processed earlier) */
        else
            aeadFilterWrite(this, NULL, 0, error);
    }

    /* Notify the downstream file it must close as well. */
    passThroughClose(this, error);

    /* TODO: free all resources includiong ctx and cipher */
    if (this->cipherBuf != NULL)
        free(this->cipherBuf);
    if (this->plainBuf != NULL)
        free(this->plainBuf);
    if (this->ctx != NULL)
        EVP_CIPHER_CTX_free(this->ctx);
    if (this->cipher != NULL)
        EVP_CIPHER_free(this->cipher);
    free(this);
}


/*
 * Seek to the specified block boundary and return the new plainPosition.
 * Exception for seeking to the end of file, where the new plainPosition
 * is at the beginning of the last partial block returning the file size.
 */
pos_t aeadFilterSeek(AeadFilter *this, pos_t plainPosition, Error *error)
{
    debug("aeadFilterSeek: plainPosition=%llu  position=%llu\n", (off_t)plainPosition, this->position);
    size_t partialSize = 0;

    /* If seeking to the end, ... */
    if (plainPosition == FILE_END_POSITION)
    {
        /* Get the file size */
        size_t cipherSize = passThroughSeek(this, FILE_END_POSITION, error);
        size_t dataSize = cipherSize - this->headerSize;
        if (dataSize == 0)
            return filterError(error, "Encrypted file truncated - missing final record");

        /* Position self at beginning of last record */
        if (dataSize % this->plainSize == 0)
            dataSize = dataSize - this->cipherSize;
        dataSize = sizeRoundDown(dataSize, this->cipherSize);
        passThroughSeek(this, this->headerSize + dataSize, error);

        /* update our plaintext position. */
        this->recordNr = dataSize / this->cipherSize;
        this->position = this->recordNr * this->plainSize;

        /* Read and decrypt the last record. Because of possible padding, we need to decrypt to determine size. */
        partialSize = aeadFilterRead(this, this->plainBuf, this->plainSize, error);

        /* If the last record was size 0, then we just got an EOF.  Ignore it. */
        if (errorIsEOF(*error))
            *error = errorOK;

        /* Now we know the position we really want - end of last full block. */
        /*  and we know the size of the last partial block */
        plainPosition = this->recordNr * this->plainSize;
    }

    /* Verify we are seeking to a block boundary */
    if (plainPosition % this->plainSize != 0)
        return filterError(error, "Must seek to a block boundary");

    /* Set the new block number and go there in the downstream file. */
    this->recordNr = plainPosition / this->plainSize;
    this->position = plainPosition;
    pos_t cipherPosition = this->headerSize + this->recordNr * this->cipherSize;
    debug("aeadSeek: plainPosition=%llu  cipherPosition=%llu recordNr=%zu\n", plainPosition, cipherPosition, this->recordNr);
    passThroughSeek(this, cipherPosition, error);

    /* Return the new plainPosition, adding in partialSize if requesting FILE_END_POSITION */
    return plainPosition + partialSize;
}


size_t aeadFilterBlockSize(AeadFilter *this, size_t plainSize, Error *error)
{
    /* Negotiate with the next stage. Because we have a variable length header, we must talk to byte stream. */
    size_t nextSize = passThroughBlockSize(this, 1, error);
    if (nextSize != 1)
        return filterError(error, "AEAD Encryption must be followed by a byte stream.");

    /* Once our successor is initialized, we can read/write the file header */
    aeadConfigure(this, error);

    /* Our plaintext block size is fixed. Calculate the corresponding encrypted size. */
    this->cipherSize = this->plainSize + paddingSize(this, this->plainSize) + this->tagSize;

    /* Allocate buffers to hold records of encrypted/decrypted data. */
    this->cipherBuf = malloc(this->cipherSize);
    this->plainBuf = malloc(this->plainSize); /* Big enough to hold header */

    /* Tell the previous stage they must accommodate our plaintext block size. */
    return this->plainSize;
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

AeadFilter *aeadFilterNew(char *cipherName, size_t recordSize, Byte *key, size_t keySize, void *next)
{
    AeadFilter *this = malloc(sizeof(AeadFilter));

    /* Save the key without overwriting memory. We'll verify the key length later. */
    assert(keySize <= sizeof(this->key));
    this->keySize = keySize;
    memcpy(this->key, key, keySize);

    /* Save defaults for creating a new file. Otherwise, we'll read them from file header. */
    strlcpy(this->cipherName, cipherName, sizeof(this->cipherName));
    this->plainSize = recordSize;

    return filterInit(this, &aeadFilterInterface, next);
}


/*
 * Configure encryption, whether creating or reading.
 */
void aeadConfigure(AeadFilter *this,  Error *error)
{
    /* We use a special record number for headers */
    this->recordNr = HEADER_SEQUENCE_NUMBER;

    /* Try to read an existing header */
    aeadHeaderRead(this, error);

    /* If empty file, then try to write a new header */
    if (this->writable && errorIsEOF(*error))
    {
        *error = errorOK;
        aeadHeaderWrite(this, error);
    }
    else if (isError(*error))
        filterError(error, "AEAD encryption can't read header");

    /* Start the first data record with sequence number zero */
    this->recordNr = 0;
}


void aeadHeaderRead(AeadFilter *this, Error *error)
{
    if (isError(*error))
        return;

    /* Read the header */
    Byte header[MAX_AEAD_HEADER_SIZE] = {0};
    size_t headerSize = passThroughReadSized(this, header, sizeof(header), error);
    if (errorIsEOF(*error))
        return;
    if (isError(*error))
        return (void)filterError(error, "Unable to read encrypted file header");

    /* Remember the full header size as stored in the file. Since it was a "sized" write, add 4 bytes for the size field. */
    this->headerSize = headerSize + 4;

    /* Extract the various fields from the header, ensuring safe memory references */
    Byte *bp = header;
    Byte *end = header + headerSize;

    /* Get the plain text record size for this encrypted file. */
    this->plainSize = unpack4(&bp, end);
    if (this->plainSize > MAX_RECORD_SIZE)
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

    /* Validate the header after removing the empty block and tag. */
    Byte plainEmpty[0];
    size_t validateSize = this->headerSize - this->tagSize - 1 - emptySize - 1;
    aead_decrypt(this, plainEmpty, sizeof(plainEmpty),
         header, validateSize, emptyBlock, emptySize, tag, error);

    /* set the ciphertext block size. TODO: Make calculation a function. */
    this->cipherSize = this->plainSize + paddingSize(this, this->cipherSize) + this->tagSize;
}


void aeadHeaderWrite(AeadFilter *this, Error *error)
{
    /* Configure the cipher parameters. */
    aeadCipherSetup(this, this->cipherName, error);
    if (isError(*error))
        return;

    /* Generate an initialization vector. */
    RAND_bytes(this->iv, (int)this->ivSize);

    /* Declare a local buffer to hold the header we're creating */
    Byte header[MAX_AEAD_HEADER_SIZE];
    Byte *bp = header;
    Byte *end = header + sizeof(header);

    /* Plaintext record size for this file. */
    pack4(&bp, end, this->plainSize);

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
        return (void)filterError(error, "Size of cipher padding for empty record was miscalculated");

    /* Add the empty block and tag to the header */
    pack1(&bp, end, emptyCipherSize);
    packBytes(&bp, end, emptyCiphertext, emptyCipherSize);
    pack1(&bp, end, this->tagSize);
    packBytes(&bp, end, tag, this->tagSize);

    /* Verify we haven't overflowed the header */
    if (bp > end)
        return (void)filterError(error, "Encryption file header was too large.");

    /* Write the header to the output file */
    passThroughWriteSized(this, header, bp - header, error);

    /* Remember the header size. Since we did a "sized" write, add 4 bytes for the record size. */
    this->headerSize = bp - header + 4;
}


/**
 * Helper function to get the cipher details.
 */
void aeadCipherSetup(AeadFilter *this, char *cipherName, Error *error)
{
    /* Save the cipher name. The name must be an exact match to a libcrypto name. */
    /* TODO KLUDGE: The cipher name is already in this->cipherName. Don't copy if already there ... FIX IT!  */
    if (this->cipherName != cipherName) /* comparing pointers */
        strlcpy(this->cipherName, cipherName, sizeof(this->cipherName));

    /* Create an OpenSSL cipher context. */
    this->ctx = EVP_CIPHER_CTX_new();

    /* Lookup cipher by name. */
    this->cipher = EVP_CIPHER_fetch(NULL, this->cipherName, NULL);
    if (this->cipher == NULL)
        return (void)filterError(error, "Encryption problem - cipher name not recognized");

    /* Verify cipher is an AEAD cipher */
    /* TODO: should be possible */

    /* Get the properties of the selected cipher */
    this->ivSize = EVP_CIPHER_iv_length(this->cipher);
    if (this->keySize != EVP_CIPHER_key_length(this->cipher))
        return (void)filterError(error, "Cipher key is the wrong size");
    this->blockSize = EVP_CIPHER_block_size(this->cipher);
    this->hasPadding = (this->blockSize != 1);
    this->tagSize = 16;  /* TODO: EVP_CIPHER_CTX_get_tag_length(this->ctx); But only after initialized. */
}


/*
 * Calculate how much padding is added when encrypting a record.
 */
size_t paddingSize(AeadFilter *this, size_t recordSize)
{
    return (this->hasPadding)
       ? this->blockSize - (recordSize % this->blockSize)
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

    //debug("Encrypt: plainText='%.*s' plainSize=%zu  cipher=%s\n", (int)sizeMin(plainSize,64), plainText, plainSize, this->cipherName);
    debug("Encrypt: plainSize=%zu  cipher=%s plainText='%.*s'\n",
          plainSize, this->cipherName, (int)plainSize, plainText);
    /* Reinitialize the encryption context to start a new record */
    EVP_CIPHER_CTX_reset(this->ctx);

    /* Generate nonce by XOR'ing the initialization vector with the sequence number */
    Byte nonce[EVP_MAX_IV_LENGTH];
    generateNonce(nonce, this->iv, this->ivSize, this->recordNr);
    debug("Encrypt: iv=%s  recordNr=%zu  nonce=%s  key=%s\n",
          asHex(this->iv, this->ivSize), this->recordNr, asHex(nonce, this->ivSize), asHex(this->key, this->keySize));

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

    debug("Encrypt: tag=%s cipherSize=%d cipherText=%.128s \n", asHex(tag, this->tagSize), cipherUpdateSize+cipherFinalSize, asHex(cipherText, cipherUpdateSize + cipherFinalSize));
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
    debug("Decrypt:  cipherSize=%zu  cipher=%s  cipherText=%.128s \n", cipherSize, this->cipherName,  asHex(cipherText, cipherSize));
    /* Reinitialize the encryption context to start a new record */
    EVP_CIPHER_CTX_reset(this->ctx);

    /* Generate nonce by XOR'ing the initialization vector with the sequence number */
    Byte nonce[EVP_MAX_IV_LENGTH];
    generateNonce(nonce, this->iv, this->ivSize, this->recordNr);
    debug("Decrypt: iv=%s  recordNr=%zu  nonce=%s  key=%s  tag=%s\n",
          asHex(this->iv, this->ivSize), this->recordNr, asHex(nonce, this->ivSize), asHex(this->key, this->keySize), asHex(tag, this->tagSize));

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
    size_t plainActual = plainUpdateSize + plainFinalSize;
    //debug("Decrypt: plainText='%.*s' plainActual=%d\n", (int)sizeMin(plainActual,64), plainText, plainActual);
    debug("Decrypt:  plainActual=%zu plainText='%.*s'\n", plainActual, (int)plainActual, plainText);
    return plainActual;
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
    if (errorIsOK(*error) || errorIsEOF(*error))
        *error = (Error){.code=errorCodeFilter, .msg=ERR_error_string(ERR_get_error(), NULL)};
    return 0;
}
