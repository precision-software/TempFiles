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
static const Error errorBadKeyLen = (Error){.code=errorCodeIoStack, .msg="Unexpected Key or IV length."};
static const Error errorBadDecryption = (Error) {.code=errorCodeIoStack, .msg="Unable to decrypt current buffer"};
static size_t openSSLError(Error *error);
void generateNonce(Byte *nonce, Byte *iv, size_t ivSize, size_t seqNr);
size_t aead_encrypt(AeadFilter *this, const Byte *plainText, size_t plainSize, Byte *header,
                  size_t headerSize, Byte *cipherText, size_t cipherSize,Byte *tag, Error *error);
size_t aead_decrypt(AeadFilter *this, Byte *plainText, size_t plainSize, Byte *header,
                  size_t headerSize, Byte *cipherText, size_t cipherSize, Byte *tag, Error *error);
void aeadCipherSetup(AeadFilter *this, char *cipherName, Error *error);
void aeadConfigure(AeadFilter *this, Error *error);
void aeadHeaderRead(AeadFilter *this, Error *error);
void aeadHeaderWrite(AeadFilter *this, Error *error);
size_t paddingSize(AeadFilter *this, size_t blockSize);
off_t aeadFilterSeek(AeadFilter *this, off_t position, Error *error);

/**
 * Converter structure for encrypting and decrypting TLS Blocks.
 */
#define MAX_CIPHER_NAME 64
#define MAX_AEAD_HEADER_SIZE 1024
#define HEADER_SEQUENCE_NUMBER ((size_t)-1)
struct AeadFilter
{
    Filter filter;

    /* Configuration. */
    Byte key[EVP_MAX_KEY_LENGTH];       /* The key for encrypting/decrypting */
    char cipherName[MAX_CIPHER_NAME];   /* The name of the cipher, if encrypting a new file. */
    size_t blockSize;                   /* The plaintext block size, if encrypting a new file */

    /* Cipher State (based on the encryption algorithm and the file header) */
    size_t keySize;              /* The size of the key in bytes */
    size_t ivSize;               /* Size of the initialization vector, typically same as encryptSize */
    size_t cipherBlockSize;      /* Size of the cipher block. (typically 16 bytes for AES) */
    size_t tagSize;              /* Size of the MAC tag to authenticate the encrypted block. */
    bool hasPadding;             /* Whether cipher block padding is added to the encrypted blocks */
    Byte iv[EVP_MAX_IV_LENGTH];  /* The initialization vector for the sequence of blocks. */
    EVP_CIPHER *cipher;          /* The libcrypto cipher structure */
    EVP_CIPHER_CTX *ctx;         /* libcrypto context. */

    /* Our state */
    size_t headerSize;            /* Size of the header we read/wrote to the encrypted file */
    size_t blockNr;               /* The block sequence number, starting at 0 and incrementing. */
    size_t encryptSize;           /* The size of the encrypted blocks */
	size_t decryptSize;           /* The size of the decrypted blocks */
    Byte *cipherBuf;              /* Buffer to hold the current encrypted block */
    bool readable;
    bool writable;
	bool open;

    /* Our plaintext positions used to decide if we need to add zero length block at end. */
    off_t fileSize;               /* (MAX if unknown, 0 if truncated, actual otherwise) */
    off_t position;
    off_t maxReadPosition;        /* Biggest position after reading */
    off_t maxWritePosition;       /* Biggest position after writing */

    Byte *plainBuf;                /* A buffer to temporarily hold a decrypted block */
};


/**
 * Open an encrypted file.
 * @param path - the path or file name.
 * @param oflag - the open flags, say O_RDONLY or O_CREATE.
 * @param mode - if creating a file, the permissions.
 * @return - Error status.
 */
void aeadFilterOpen(AeadFilter *this, const char *path, int oflags, int mode, Error *error)
{
    /* We need to read header, even if otherwise write only */
    if ( (oflags & O_ACCMODE) == O_WRONLY)
        oflags = (oflags & ~O_ACCMODE) | O_RDWR;

    /* Open the downstream file */
    passThroughOpen(this, path, oflags, mode, error);
    if (isError(*error))
        return;

    /* We do NOT support append mode directly. Use Buffering if O_APPEND is needed. */
    if ((oflags & O_APPEND) != 0)
        return (void)ioStackError(error, "Can't directly append to encrypted file - use buffering");


    /* Is the file readable/writable? */
    this->writable = (oflags & O_ACCMODE) != O_RDONLY;
    this->readable = (oflags & O_ACCMODE) != O_WRONLY;

    /*
     * Note we are positioned at the first block and we've done no I/O so far.
     * We will wait until aeadBlockSize to read the header of the encrypted file
     * and configure encryption.
     */
    this->blockNr = 0;
    this->maxReadPosition = 0;
    this->maxWritePosition = 0;
    this->fileSize = (oflags & O_TRUNC)? 0 : FILE_END_POSITION;
    this->position = 0;
    this->cipherBuf = NULL;
    this->plainBuf = NULL;
	this->cipher = NULL;
	this->ctx = NULL;

	this->open = true;
}


/**
 * Read a block of encrypted data into our internal buffer, placing plaintext into the caller's buffer.
 */
size_t aeadFilterRead(AeadFilter *this, Byte *buf, size_t size, Error *error)
{
    debug("aeadFilterRead: size=%zu  position=%llu maxWrite=%llu maxRead=%llu fileSize=%lld\n",
          size, this->position, this->maxWritePosition, this->maxReadPosition, (off_t)this->fileSize);
    /* Read a block of downstream encrypted text into our buffer. */
    size_t actual = passThroughReadAll(this, this->cipherBuf, this->encryptSize, error);
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
    if (plainSize < this->decryptSize)
        this->fileSize = this->position;

    /* If partial block, probe to make sure the file is really EOF. */
    if (plainSize < this->decryptSize)
    {
        passThroughRead(this, this->cipherBuf, 1, error);
        if (!errorIsEOF(*error))
            return ioStackError(error, "Encrypted file has extra data appended.");
        *error = errorOK;
    }

    /* If the partial block was empty, then we're at EOF now */
    if (plainSize == 0)
        return setError(error, errorEOF);

    /* Track our position for EOF handling */
    this->maxReadPosition = sizeMax(this->maxReadPosition, this->blockNr * this->decryptSize + plainSize);

    /* Bump the block sequence number if full block read */
    if (plainSize == this->decryptSize)
        this->blockNr++;

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
size_t aeadFilterWrite(AeadFilter *this, const Byte *buf, size_t size, Error *error)
{
    debug("aeadFilterWrite: size=%zu  position=%llu maxWrite=%llu maxRead=%llu fileSize=%lld\n",
          size, this->position, this->maxWritePosition, this->maxReadPosition, (off_t)this->fileSize);
    if (isError(*error))
        return 0;

    /* Encrypt one record of data into our buffer */
    Byte tag[EVP_MAX_MD_SIZE];
    size_t plainSize = sizeMin(size, this->decryptSize);
    size_t cipherSize = aead_encrypt(this, buf, plainSize, NULL, 0, this->cipherBuf, this->encryptSize - this->tagSize, tag, error);

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
    this->blockNr++;
	debug("aeadFilterWrite(done): msg=%s", error->msg);

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

	/* If we're already closed, then simply return */
	if (!this->open) return;

    /* Do we need to read or write a final empty record? */
    /* CASE: NO. file is bigger then max write (because we read it) */
    if (this->maxReadPosition > this->maxWritePosition)
        ;

    /* CASE: NO. the biggest write was a partial block. No need to add empty block */
    else if (this->maxWritePosition % this->decryptSize != 0)
        ;

    /* CASE: NO. We know the file size and it is bigger than last write */
    else if (this->fileSize != FILE_END_POSITION && this->fileSize > this->maxWritePosition)
        ;

    else
    {
        /* CASE: NO. downstream file size is bigger than our highest write. (PADDING PROBLEMS!)*/
        size_t actualSize = passThroughSeek(this, FILE_END_POSITION, error);
        size_t expectedSize = this->maxWritePosition / this->decryptSize * this->encryptSize + this->headerSize;
        if (actualSize > expectedSize)
            ;

        /* OTHERWISE: YES. Add an empty block to the end. (Final record is full, since partials processed earlier) */
        else
            aeadFilterWrite(this, NULL, 0, error);
    }

    /* Notify the downstream file it must close as well. */
    passThroughClose(this, error);

    /* TODO: free all resources includiong ctx and cipher. Maybe we should close? */
    if (this->cipherBuf != NULL)
        free(this->cipherBuf);
	this->cipherBuf = NULL;
    if (this->plainBuf != NULL)
        free(this->plainBuf);
	this->plainBuf = NULL;
    if (this->ctx != NULL)
        EVP_CIPHER_CTX_free(this->ctx);
	this->ctx = NULL;
    if (this->cipher != NULL)
        EVP_CIPHER_free(this->cipher);
	this->cipher = NULL;

	this->open = false;
	debug("aeadFilterClose(done): msg=%s", error->msg);
}


/*
 * Seek to the specified block boundary and return the new plainPosition.
 * Exception for seeking to the end of file, where the new plainPosition
 * is at the beginning of the last partial block returning the file size.
 */
off_t aeadFilterSeek(AeadFilter *this, off_t plainPosition, Error *error)
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
            return ioStackError(error, "Encrypted file truncated - missing final record");

        /* Position self at beginning of last record */
        if (dataSize % this->decryptSize == 0)
            dataSize = dataSize - this->encryptSize;
        dataSize = sizeRoundDown(dataSize, this->encryptSize);
        passThroughSeek(this, this->headerSize + dataSize, error);

        /* update our plaintext position. */
        this->blockNr = dataSize / this->encryptSize;
        this->position = this->blockNr * this->decryptSize;

        /* Read and decrypt the last record. Because of possible padding, we need to decrypt to determine size. */
        partialSize = aeadFilterRead(this, this->plainBuf, this->decryptSize, error);

        /* If the last record was size 0, then we just got an EOF.  Ignore it. */
        if (errorIsEOF(*error))
            *error = errorOK;

        /* Now we know the position we really want - end of last full block. */
        /*  and we know the size of the last partial block */
        plainPosition = this->blockNr * this->decryptSize;
    }

    /* Verify we are seeking to a block boundary */
    if (plainPosition % this->decryptSize != 0)
        return ioStackError(error, "Must seek to a block boundary");

    /* Set the new block number and go there in the downstream file. */
    this->blockNr = plainPosition / this->decryptSize;
    this->position = plainPosition;
    off_t cipherPosition = this->headerSize + this->blockNr * this->encryptSize;
    debug("aeadSeek: plainPosition=%llu  cipherPosition=%llu blockNr=%zu\n", plainPosition, cipherPosition, this->blockNr);
    passThroughSeek(this, cipherPosition, error);

    /* Return the new plainPosition, adding in partialSize if requesting FILE_END_POSITION */
    return plainPosition + partialSize;
}


size_t aeadFilterBlockSize(AeadFilter *this, size_t plainSize, Error *error)
{
    /* Negotiate with the next stage. Because we have a variable length header, we must talk to byte stream. */
    size_t nextSize = passThroughBlockSize(this, 1, error);
    if (nextSize != 1)
        return ioStackError(error, "AEAD Encryption must be followed by a byte stream.");

    /* Once our successor is initialized, we can read/write the file header */
    aeadConfigure(this, error);

    /* Our plaintext block size has been specified. Calculate the corresponding encrypted size. */
    this->encryptSize = this->decryptSize + paddingSize(this, this->decryptSize) + this->tagSize;

    /* Allocate buffers to hold records of encrypted/decrypted data. */
    this->cipherBuf = malloc(this->encryptSize);
    this->plainBuf = malloc(this->decryptSize); /* Big enough to hold header */

    /* Tell the previous stage they must accommodate our plaintext block size. */
    return this->decryptSize;
}

void *aeadFilterClone(AeadFilter *this)
{
	void *next = passThroughClone(this);
	return aeadFilterNew(this->cipherName, this->blockSize, this->key, this->keySize, next);
}

void aeadFilterFree(AeadFilter *this)
{

	/* Repeated from Close. Do we need it?  Or just assert they are NULL? */
	if (this->cipherBuf != NULL)
		free(this->cipherBuf);
	this->cipherBuf = NULL;
	if (this->plainBuf != NULL)
		free(this->plainBuf);
	this->plainBuf = NULL;
	if (this->ctx != NULL)
		EVP_CIPHER_CTX_free(this->ctx);
	this->ctx = NULL;
	if (this->cipher != NULL)
		EVP_CIPHER_free(this->cipher);
	this->cipher = NULL;

	passThroughFree(this);
	free(this);
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
		.fnClone = (FilterClone) aeadFilterClone,
		.fnFree = (FilterFree) aeadFilterFree,
};

AeadFilter *aeadFilterNew(char *cipherName, size_t blockSize, Byte *key, size_t keySize, void *next)
{
    AeadFilter *this = malloc(sizeof(AeadFilter));

    /* Save the key without overwriting memory. We'll verify the key length later. */
    assert(keySize <= sizeof(this->key));
    this->keySize = keySize;
    memcpy(this->key, key, keySize);

    /* Save defaults for creating a new file. Otherwise, we'll read them from file header. */
    strlcpy(this->cipherName, cipherName, sizeof(this->cipherName));
    this->blockSize = blockSize;

	this->open = false;

    return filterInit(this, &aeadFilterInterface, next);
}


/*
 * Configure encryption, whether creating or reading.
 */
void aeadConfigure(AeadFilter *this,  Error *error)
{
    /* We use a special record number for headers */
    this->blockNr = HEADER_SEQUENCE_NUMBER;

    /* Try to read an existing header */
    aeadHeaderRead(this, error);

    /* If empty file, then try to write a new header */
    if (this->writable && errorIsEOF(*error))
    {
        *error = errorOK;
        aeadHeaderWrite(this, error);
    }
    else if (isError(*error))
		ioStackError(error, "AEAD encryption can't read header");

    /* Start the first data block with sequence number zero */
    this->blockNr = 0;
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
        return (void) ioStackError(error, "Unable to read encrypted file header");

    /* Remember the full header size as stored in the file. Since it was a "sized" write, add 4 bytes for the size field. */
    this->headerSize = headerSize + 4;

    /* Extract the various fields from the header, ensuring safe memory references */
    Byte *bp = header;
    Byte *end = header + headerSize;

    /* Get the plain text record size for this encrypted file. */
    this->decryptSize = unpack4(&bp, end);
    if (this->decryptSize > MAX_BLOCK_SIZE)
        return (void) ioStackError(error, "AEAD record size in header is > 16MB");

    /* Get the cipher name */
    size_t nameSize = unpack1(&bp, end);
    if (nameSize > sizeof(this->cipherName) - 1)  /* allow for null termination */
        return (void) ioStackError(error, "Cipher name in header is too large");
    unpackBytes(&bp, end, (Byte *)this->cipherName, nameSize);
    this->cipherName[nameSize] = '\0';

    /* Get the initialization vector */
    this->ivSize = unpack1(&bp, end);
    if (this->ivSize > sizeof(this->iv))
        return (void) ioStackError(error, "Initialization vector is too large");
    unpackBytes(&bp, end, this->iv, this->ivSize);

    /* Get the empty cipher text block */
    Byte emptyBlock[EVP_MAX_BLOCK_LENGTH];
    size_t emptySize = unpack1(&bp, end);
    if (emptySize > sizeof(emptyBlock))
        return (void) ioStackError(error, "Empty cipher block in header is too large");
    unpackBytes(&bp, end, emptyBlock, emptySize);

    /* Get the MAC tag */
    Byte tag[EVP_MAX_MD_SIZE];
    this->tagSize = unpack1(&bp, end);
    if (this->tagSize > sizeof(tag))
        return (void) ioStackError(error, "Authentication tag is too large");
    unpackBytes(&bp, end, tag, this->tagSize);

    /* Verify we haven't overflowed. Ideally, we should have bp == end */
    if (bp > end)
        return (void) ioStackError(error, "Invalid AEAD header in file");

    /* Lookup the cipher and its parameters. */
    aeadCipherSetup(this, this->cipherName, error);
    if (isError(*error))
        return;

    /* Validate the header after removing the empty block and tag. */
    Byte plainEmpty[0];
    size_t validateSize = this->headerSize - this->tagSize - 1 - emptySize - 1;
    aead_decrypt(this, plainEmpty, sizeof(plainEmpty),
         header, validateSize, emptyBlock, emptySize, tag, error);

    /* Calculate the ciphertext size for a full plaintext record */
    this->encryptSize = this->decryptSize + paddingSize(this, this->decryptSize) + this->tagSize;
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
	this->decryptSize = this->blockSize;
    pack4(&bp, end, this->decryptSize);

    /* Cipher name */
    pack1(&bp, end, strlen(this->cipherName));
    packBytes(&bp, end, (Byte *)this->cipherName, strlen(this->cipherName));

    /* Initialization vector */
    pack1(&bp, end, this->ivSize);
    packBytes(&bp, end, this->iv, this->ivSize);

    /* Verify we haven't overflowed our buffer. */
    if (bp > end)
        return (void) ioStackError(error, "Trying to write a header which is too large");

    /* Encrypt an empty plaintext block and authenticate the header. */
    Byte emptyCiphertext[EVP_MAX_BLOCK_LENGTH];
    Byte emptyPlaintext[0];
    Byte tag[EVP_MAX_MD_SIZE];
    size_t emptyCipherSize = aead_encrypt(this, emptyPlaintext, 0, header, end-header,
                                          emptyCiphertext, sizeof(emptyCiphertext), tag, error);
    if (emptyCipherSize != paddingSize(this, 0) || emptyCipherSize > 256)
        return (void) ioStackError(error, "Size of cipher padding for empty record was miscalculated");

    /* Add the empty block and tag to the header */
    pack1(&bp, end, emptyCipherSize);
    packBytes(&bp, end, emptyCiphertext, emptyCipherSize);
    pack1(&bp, end, this->tagSize);
    packBytes(&bp, end, tag, this->tagSize);

    /* Verify we haven't overflowed the header */
    if (bp > end)
        return (void) ioStackError(error, "Encryption file header was too large.");

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
        return (void) ioStackError(error, "Encryption problem - cipher name not recognized");

    /* Verify cipher is an AEAD cipher */
    /* TODO: should be possible */

    /* Get the properties of the selected cipher */
    this->ivSize = EVP_CIPHER_iv_length(this->cipher);
    if (this->keySize != EVP_CIPHER_key_length(this->cipher))
        return (void) ioStackError(error, "Cipher key is the wrong size");
    this->cipherBlockSize = EVP_CIPHER_block_size(this->cipher);
    this->hasPadding = (this->cipherBlockSize != 1);
    this->tagSize = 16;  /* TODO: EVP_CIPHER_CTX_get_tag_length(this->ctx); But only after initialized. */
}


/*
 * Calculate how much padding is added when encrypting a record.
 */
size_t paddingSize(AeadFilter *this, size_t recordSize)
{
    return (this->hasPadding)
       ? this->cipherBlockSize - (recordSize % this->cipherBlockSize)
       : 0;
}


/*
 * Encrypt one record of plain text, generating one (slightly larger) record of cipher text.
 *  @param this - aaed converter
 *  @param plainText - the text to be encrypted.
 *  @param decryptSize - size of the text to be encrypted
 *  @param header - text to be authenticated but not encrypted.
 *  @param headerSize - size of header. If 0, then header can be NULL.
 *  @param cipherText - the output encrypted text
 *  @param encryptSize - the size of the buffer on input, actual size on output.
 *  @param tag - the output MAC tag of size this->tagSize
 *  @param error - Keep track of errors.
 *  @return - the actual size of the encrypted ciphertext.
 */
size_t
aead_encrypt(AeadFilter *this,
             const Byte *plainText, size_t plainSize,
             Byte *header, size_t headerSize,
             Byte *cipherText, size_t cipherSize,
             Byte *tag, Error *error)
{

    //debug("Encrypt: plainText='%.*s' decryptSize=%zu  cipher=%s\n", (int)sizeMin(decryptSize,64), plainText, decryptSize, this->cipherName);
    debug("Encrypt: decryptSize=%zu  cipher=%s plainText='%.*s'\n",
          plainSize, this->cipherName, (int)plainSize, plainText);
    /* Reinitialize the encryption context to start a new record */
    EVP_CIPHER_CTX_reset(this->ctx);

    /* Generate nonce by XOR'ing the initialization vector with the sequence number */
    Byte nonce[EVP_MAX_IV_LENGTH];
    generateNonce(nonce, this->iv, this->ivSize, this->blockNr);
    debug("Encrypt: iv=%s  blockNr=%zu  nonce=%s  key=%s\n",
          asHex(this->iv, this->ivSize), this->blockNr, asHex(nonce, this->ivSize), asHex(this->key, this->keySize));

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
        if (!EVP_CipherUpdate(this->ctx, (Byte *)cipherText, &cipherUpdateSize, plainText, (int)plainSize))
            return openSSLError(error);
    }

    /* Finalise the plaintext encryption. This can generate data, usually padding, even if there is no plain text. */
    int cipherFinalSize = (int)cipherSize - cipherUpdateSize;
    if (!EVP_CipherFinal_ex(this->ctx, (Byte *)cipherText + cipherUpdateSize, &cipherFinalSize))
        return openSSLError(error);

    /* Get the authentication tag  */
    if (!EVP_CIPHER_CTX_ctrl(this->ctx, EVP_CTRL_AEAD_GET_TAG, (int)this->tagSize, tag))
        return openSSLError(error);

    debug("Encrypt: tag=%s encryptSize=%d cipherText=%.128s \n", asHex(tag, this->tagSize), cipherUpdateSize+cipherFinalSize, asHex(cipherText, cipherUpdateSize + cipherFinalSize));
    /* Output size combines both the encyption (update) and the finalization. */
    return cipherUpdateSize + cipherFinalSize;
}

/*
 * Decrypt one record of ciphertext, generating one (slightly smaller?) record of plain text.
 * This routine implements a generic AEAD interface.
 *  @param this - aaed converter
 *  @param plainText - the text to be encrypted.
 *  @param decryptSize - size of the text to be encrypted
 *  @param header - text to be authenticated but not encrypted.
 *  @param headerSize - size of header. If 0, then header can be NULL.
 *  @param cipherText - the output encrypted text
 *  @param encryptSize - the size of the buffer on input, actual size on output.
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
    debug("Decrypt:  encryptSize=%zu  cipher=%s  cipherText=%.128s \n", cipherSize, this->cipherName,  asHex(cipherText, cipherSize));
    /* Reinitialize the encryption context to start a new record */
    EVP_CIPHER_CTX_reset(this->ctx);

    /* Generate nonce by XOR'ing the initialization vector with the sequence number */
    Byte nonce[EVP_MAX_IV_LENGTH];
    generateNonce(nonce, this->iv, this->ivSize, this->blockNr);
    debug("Decrypt: iv=%s  blockNr=%zu  nonce=%s  key=%s  tag=%s\n",
          asHex(this->iv, this->ivSize), this->blockNr, asHex(nonce, this->ivSize), asHex(this->key, this->keySize), asHex(tag, this->tagSize));

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
        *error = (Error){.code=errorCodeIoStack, .msg=ERR_error_string(ERR_get_error(), NULL)};
    return 0;
}
