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

#include "../iostack.h"
#include "../framework/debug.h"
#include "../framework/packed.h"

/* Interface */
typedef struct Aead Aead;

/* Forward references */

static bool openSSLError(int code);
void generateNonce(Byte *nonce, Byte *iv, size_t ivSize, size_t seqNr);
ssize_t aead_encrypt(Aead *this, const Byte *plainBlock, size_t plainSize, Byte *header,
                  size_t headerSize, Byte *cipherBlock, size_t cipherSize, Byte *tag, ssize_t blockNr);
ssize_t aead_decrypt(Aead *this, Byte *plainText, size_t plainSize, Byte *header,
                  size_t headerSize, Byte *cipherText, size_t cipherSize, Byte *tag, ssize_t blockNr);
bool aeadCipherSetup(Aead *this, char *cipherName);
bool aeadConfigure(Aead *this);
size_t paddingSize(Aead *this, size_t suggestedSize);
static bool needsFinalBlock(Aead *this);
off_t cryptOffset(Aead *this, off_t plainOffset);
size_t cryptSize(Aead *this, size_t plainSize);
bool aeadHeaderRead(Aead *this);
bool aeadHeaderWrite(Aead *this);
static ssize_t setOpenSSLError(Aead *this, ssize_t ret);


/**
 * Converter structure for encrypting and decrypting TLS Blocks.
 */
#define MAX_CIPHER_NAME 64
#define MAX_AEAD_HEADER_SIZE 1024
#define HEADER_SEQUENCE_NUMBER ((size_t)-1)
struct Aead
{
	/* Always at beginning of structure */
    IoStack ioStack;

    /* Configuration. */
    Byte key[EVP_MAX_KEY_LENGTH];       /* The key for encrypting/decrypting */
    char cipherName[MAX_CIPHER_NAME];   /* The name of the cipher, if encrypting a new file. */
    size_t suggestedSize;                   /* The plaintext block size, if encrypting a new file */

    /* Cipher State (based on the encryption algorithm and the file header) */
    size_t keySize;              /* The size of the key in bytes */
    size_t ivSize;               /* Size of the initialization vector, typically same as cryptSize */
    size_t cipherBlockSize;      /* Size of the cipher block. (typically 16 bytes for AES) */
    size_t tagSize;              /* Size of the MAC tag to authenticate the encrypted block. */
    bool hasPadding;             /* Whether cipher block padding is added to the encrypted blocks */
    Byte iv[EVP_MAX_IV_LENGTH];  /* The initialization vector for the sequence of blocks. */
    EVP_CIPHER *cipher;          /* The libcrypto cipher structure */
    EVP_CIPHER_CTX *ctx;         /* libcrypto context. */

    /* Our state after we've opened the encrypted file */
    size_t headerSize;            /* Size of the header we read/wrote to the encrypted file */
    size_t blockNr;               /* The block sequence number, starting at 0 and incrementing. */
	Byte *cryptBuf;               /* Buffer to hold the current encrypted block */
    size_t cryptSize;             /* The size of the encrypted blocks */
	Byte *plainBuf;               /* A buffer to temporarily hold a decrypted block. */
	size_t plainSize;             /* The size of the decrypted blocks */

    bool readable;
    bool writable;
	bool open;

    /* Plaintext positions used to decide if we need to add zero length block at end. */
	bool sizeConfirmed;           /* true if we know the actual plain text file size */
    off_t fileSize;               /* actual plaintext size if confirmed, biggest seen so far if not confirmed */
    off_t maxWritePosition;       /* Biggest plaintext position after writing */
};


/**
 * Open an encrypted file.
 * @param path - the path or file name.
 * @param oflag - the open flags, say O_RDONLY or O_CREATE.
 * @param mode - if creating a file, the permissions.
 * @return - Error status.
 */
bool aeadOpen(Aead *this, const char *path, int oflags, int mode)
{

	/* Is the file readable/writable? */
	this->writable = (oflags & O_ACCMODE) != O_RDONLY;
	this->readable = (oflags & O_ACCMODE) != O_WRONLY;

	/* Even if we are write only, we need to read the file to verify header (unless O_TRUNC?) */
	if ((oflags & O_ACCMODE) == O_WRONLY)
		oflags = (oflags & ~O_ACCMODE) | O_RDWR;

	/* Clear buf pointers so we don't try to free them on error */
	this->cryptBuf = NULL;
	this->plainBuf = NULL;
	this->cipher = NULL;
	this->ctx = NULL;

    /* Open the downstream file */
    if (!fileOpen(nextStack(this), path, oflags, mode))
        return setNextError(this, false);

	/* Read the downstream file to get header information and configure encryption */
	aeadConfigure(this);
	assert(this->ctx != NULL && this->cipher != NULL);

	/* Verify our block sizes are compatible */
	if (this->cryptSize % nextStack(this)->blockSize != 0)
		return setIoStackError(this, "Aead block sizes incompatible:  ours=%zu  theirs=%zu", this->cryptSize, nextStack(this)->blockSize);
	thisStack(this)->blockSize = this->plainSize;

	/* Allocate our own buffers based on the encryption config */
	this->cryptBuf = malloc(this->cryptSize); /* TODO: memory allocation */
	this->plainBuf = malloc(this->plainSize);

	/* Track the file size as we know it so far, so we avoid having to query fileSize to get it */
    this->maxWritePosition = 0;
    this->fileSize = 0;
	this->sizeConfirmed = (oflags & O_TRUNC) != 0;

	this->open = true;
	return true;
}


/**
 * Read a block of encrypted data into our internal buffer, placing plaintext into the caller's buffer.
 */
ssize_t aeadRead(Aead *this, Byte *buf, size_t size, off_t offset, void *ctx)
{
    debug("aeadRead: size=%zu  offset=%llu maxWrite=%llu fileSize=%lld\n",
          size, offset, this->maxWritePosition, this->fileSize);

	/* If we are positioned at EOF, then return EOF */
	if (this->sizeConfirmed && offset == this->fileSize)
	{
		thisStack(this)->eof = true;
		return 0;
	}

	assert(offset % thisStack(this)->blockSize == 0);

	/* Translate our offset to our successor's offset */
	off_t cipherOffset = cryptOffset(this, offset);

    /* Read a block of downstream encrypted text into our buffer. */
    ssize_t actual = fileReadAll(nextStack(this), this->cryptBuf, this->cryptSize, cipherOffset, ctx);
    if (actual <= 0)
        return setNextError(this, actual);

    /* Extract the tag from the end of our buffer. */
    Byte tag[EVP_MAX_MD_SIZE];
    size_t cipherTextSize = actual - this->tagSize;
    memcpy(tag, this->cryptBuf + cipherTextSize, this->tagSize);

    /* Decrypt the ciphertext from our buffer into our caller's buffer */
	ssize_t blockNr = (offset / this->plainSize); /* TODO: add as parameter to aead_decrypt? */
    ssize_t plainSize = aead_decrypt(this, buf, size, NULL, 0, this->cryptBuf, cipherTextSize, tag, blockNr);
    if (plainSize < 0)
        return plainSize;

	/* Track our position for EOF handling */
    this->sizeConfirmed |= (plainSize < this->plainSize);
	this->fileSize = MAX(this->fileSize, offset + plainSize);

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
size_t aeadWrite(Aead *this, const Byte *buf, size_t size, off_t offset, void *ctx)
{
    debug("aeadWrite: size=%zu  offset=%llu maxWrite=%llu fileSize=%lld\n",
          size, offset, this->maxWritePosition, (off_t)this->fileSize);

	/* Give special error message if we are trying to do unaligned append to the file */
	if (offset % this->plainSize != 0 && this->sizeConfirmed && offset == this->fileSize)
		return (setIoStackError(this, "Attempting to append to encrypted file - must use buffering"), -1);

	/* Writing a partial block before end of file would cause corruption in the file */
	if (size < this->plainSize && offset + size < this->fileSize)
		return (setIoStackError(this, "Encryption: partial block before end of file causes corruption"), -1);

	assert(offset % thisStack(this)->blockSize == 0);

    /* Encrypt one record of data into our buffer */
    Byte tag[EVP_MAX_MD_SIZE];
    size_t plainSize = MIN(size, this->plainSize);
	ssize_t blockNr = offset / this->plainSize;
    size_t cipherSize = aead_encrypt(this, buf, plainSize, NULL, 0, this->cryptBuf, this->cryptSize - this->tagSize, tag, blockNr);

    /* Append the tag to the encrypted data */
    memcpy(this->cryptBuf + cipherSize, tag, this->tagSize);
    cipherSize += this->tagSize;

	/* Translate our offset to our successor's offset */
	off_t cipherOffset = cryptOffset(this, offset);

    /* Write the encrypted block out */
    if (fileWriteAll(nextStack(this), this->cryptBuf, cipherSize, cipherOffset, ctx) != cipherSize)
	    return setNextError(this, -1);

    /* Track our position for EOF handling */
    this->maxWritePosition = MAX(this->maxWritePosition, offset+plainSize);
	this->fileSize  = MAX(this->fileSize, this->maxWritePosition);



    return plainSize;
}

/**
 * Close this encryption stack releasing resources.
 */
bool aeadClose(Aead *this)
{
    debug("aeadClose: maxWrite=%llu fileSize=%lld\n", this->maxWritePosition, this->fileSize);

	/*
	 * If not already done, add an partial (empty) block to mark the end of encrypted data.
	 * If writing a block, we need to absolutely know our file size. needsFinalBlock()
	 * will update file size if necessary.
	 */
	if (this->open && needsFinalBlock(this))
	{
		off_t size = fileSize(this);
		if (size == -1)
			return false;
		fileWrite(this, NULL, 0, size, this->ctx);  /* sets errno and msg */
	}

    /* Close the downstream file (no harm if already closed), but don't overwrite previous errors. */
    if (!fileClose(nextStack(this)))
		if (!fileError(this))
			setNextError(this, false);

    /* Free memory resources allocated during Open */
    if (this->cryptBuf != NULL)
        free(this->cryptBuf);
	this->cryptBuf = NULL;
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
	debug("aeadClose(done)");
	return true;
}


/*
 * Do we need to write a final empty block?
 * This is a complicated question because we may have been
 * writing blocks in random order, and the downstream file
 * may or may not already have a partial block at the end.
 * This code tries a series of tests, ranging from cheapest
 * to most expensive. In the end, it might overwrite an existing
 * empty record, but it always ensures there is a final, partial
 * block at the end of the file.
 */
static bool needsFinalBlock(Aead *this)
{
	/* file is read only. No need.*/
	if (!this->writable) return false;

	/* We didn't overwrite the end of the file. No need. */
	if (this->fileSize > this->maxWritePosition) return false;

	/* The biggest I/O we know of was a partial block. No need. */
	if (this->fileSize % this->plainSize != 0) return false;

	/* If our file size was accurate, then we DO need to write a final block */
	if (this->sizeConfirmed)  return true;

	/* Downstream file has more blocks than we wrote. No need. */
	off_t nextSize = fileSize(nextStack(this));
	if (cryptOffset(this, this->fileSize) < nextSize) return false;

	 /* Get accurate file size info and retry */
	 this->fileSize = fileSize(this);
	 this->sizeConfirmed = true;

	 /* The last block definitely was a partial block. No need. */
	 if (this->fileSize % this->plainSize != 0) return false;

	 /* The downstream file has extra data, presumably an empty block. No need. */
	 if (cryptOffset(this, this->fileSize) < nextSize) return false;

     /* If here, then we need to write a final empty block */
	 return true;
}

/*
 * Return the size of the plaintext file.
 * Note this is not a trivial calculation. In some cases
 * it requires decrypting the final block to get an accurate size.
 */
off_t aeadSize(Aead *this)
{
	debug("aeadSize  confirmed=%d  size=%lld\n", this->sizeConfirmed, this->fileSize);
	/* If we already know the file size, then we are done */
	if (this->sizeConfirmed)
		return this->fileSize;

	/* Get the index of the last cipher block in the downstream encrypted file */
	off_t cryptFileSize = fileSize(nextStack(this));
	if (cryptFileSize < 0)
		return setNextError(this, -1);
	off_t lastBlock = (cryptFileSize - this->headerSize) / this->cryptSize;
	if ((cryptFileSize - this->headerSize) % this->headerSize == 0)
		lastBlock--;

	/* Read and decrypt the last block. Because of possible padding, we need to decrypt to determine size. */
	/* TODO: what value for ctx? TODO: Don't need to read last block if there is no fill */
	ssize_t lastSize = aeadRead(this, this->plainBuf, this->plainSize, lastBlock * this->plainSize, this->ctx);
	if (lastSize < 0)
		return -1;

    /* Cache the file size so we know it in the future */
    this->fileSize = lastBlock * this->plainSize + lastSize;
	this->sizeConfirmed = true;

	/* done */
	debug("aeadSize (done) fileSize=%lld  lastSize=%zd\n", this->fileSize, lastSize);
	return this->fileSize;
}

off_t aeadTruncate(Aead *this, off_t offset)
{
	errno = EINVAL;
	return setSystemError(this, -1, "aeadTruncate not implemented");
}

bool aeadSync(Aead *this)
{
	errno = EINVAL;
	return setSystemError(this, -1, "aeadSync not implemented");
}

/**
 * Abstract interface for the encryption ioStack.
 */
IoStackInterface aeadInterface = {
	.fnOpen = (IoStackOpen) aeadOpen,
	.fnRead = (IoStackRead) aeadRead,
	.fnWrite = (IoStackWrite) aeadWrite,
	.fnClose = (IoStackClose) aeadClose,
	.fnTruncate = (IoStackTruncate) aeadTruncate,
	.fnSize = (IoStackSize) aeadSize,
	.fnSync = (IoStackSync) aeadSync,
};

/*
 * Create a new aead encryption/decryption filter
 */
IoStack *aeadNew(char *cipherName, size_t suggestedSize, Byte *key, size_t keySize, void *next)
{
    Aead *this = malloc(sizeof(Aead));
	*this = (Aead) {
		.suggestedSize = suggestedSize,
		.keySize = keySize,
		.ioStack = (IoStack) {
			.iface = &aeadInterface,
			.next = next,
		}
	};

    /* Copy in the key and cipher name without overwriting memory. We'll validate later. */
    memcpy(this->key, key, MIN(keySize, sizeof(this->key)));
    strlcpy(this->cipherName, cipherName, sizeof(this->cipherName));

    return thisStack(this);
}


/*
 * Calculate the size of an encrypted block give the size of the plaintext block
 */
size_t cryptSize(Aead *this, size_t plainSize)
{
	return plainSize + this->tagSize + paddingSize(this, plainSize);
}

/*
 * Calculate the file offset of an encrypted block, given the file offset of a plaintext block.
 * Note this only works for block boundaries.
 */
off_t cryptOffset(Aead *this, off_t plainOffset)
{
	return plainOffset / this->plainSize * this->cryptSize + this->headerSize;
}

/*
 * Configure encryption, whether creating or reading.
 */
bool aeadConfigure(Aead *this)
{
    /* If able to read header (or error) then done */
	fileClearError(this);
    if (aeadHeaderRead(this))
		return !fileError(this);

    /* If no header and the file isn't writable, then we're done. */
	if (!this->writable)
		return setIoStackError(this, "Readonly file doesn't have encryption header");

	/* Write out a new header */
	return aeadHeaderWrite(this);
}


/*
 * Read the header from the encrypted file.
 * Returns true if header actually read, false if EOF or error occurred.
 * Need to test for EOF with fileEOF()
 */
bool aeadHeaderRead(Aead *this)
{

    /* Read the header */
    Byte header[MAX_AEAD_HEADER_SIZE] = {0};
    ssize_t headerSize = fileReadSized(nextStack(this), header, sizeof(header), 0, this->ctx );
	if (headerSize <= 0)
		return setNextError(this, false);

    /* Remember the full header size as stored in the file. Since it was a "sized" write, add 4 bytes for the size field. */
    this->headerSize = headerSize + 4;

    /* Extract the various fields from the header, ensuring safe memory references */
    Byte *bp = header;
    Byte *end = header + headerSize;

    /* Get the plain text record size for this encrypted file. */
    this->plainSize = unpack4(&bp, end);
    if (this->plainSize > MAX_BLOCK_SIZE)
        return setIoStackError(this, "AEAD header size (%zu) exceeds %zu", this->plainSize, MAX_BLOCK_SIZE);

    /* Get the cipher name */
    size_t nameSize = unpack1(&bp, end);
    if (nameSize > sizeof(this->cipherName) - 1)  /* allow for null termination */
        return setIoStackError(this, "Cipher name in header is too large");
    unpackBytes(&bp, end, (Byte *)this->cipherName, nameSize);
    this->cipherName[nameSize] = '\0';

    /* Get the initialization vector */
    this->ivSize = unpack1(&bp, end);
    if (this->ivSize > sizeof(this->iv))
        return setIoStackError(this, "Initialization vector size (%zu) exceeeds %zu", this->ivSize, sizeof(this->iv));
    unpackBytes(&bp, end, this->iv, this->ivSize);

    /* Get the empty cipher text block */
    Byte emptyBlock[EVP_MAX_BLOCK_LENGTH];
    size_t emptySize = unpack1(&bp, end);
    if (emptySize > sizeof(emptyBlock))
        return setIoStackError(this, "Empty cipher block in header is too large");
    unpackBytes(&bp, end, emptyBlock, emptySize);

    /* Get the MAC tag */
    Byte tag[EVP_MAX_MD_SIZE];
    this->tagSize = unpack1(&bp, end);
    if (this->tagSize > sizeof(tag))
        return setIoStackError(this, "Authentication tag is too large");
    unpackBytes(&bp, end, tag, this->tagSize);

    /* Verify we haven't overflowed. Ideally, we should have bp == end */
    if (bp > end)
        return setIoStackError(this, "Invalid AEAD header in file");

    /* Lookup the cipher and its parameters. */
    if (!aeadCipherSetup(this, this->cipherName))
        return false;

    /* Validate the header after removing the empty block and tag. */
    Byte plainEmpty[0];
    size_t validateSize = this->headerSize - this->tagSize - 1 - emptySize - 1;
    if (!aead_decrypt(this, plainEmpty, sizeof(plainEmpty),
         header, validateSize, emptyBlock, emptySize, tag, HEADER_SEQUENCE_NUMBER))
		return -1;

    /* Cache the ciphertext size for a full plaintext record */
    this->cryptSize = cryptSize(this, this->plainSize);
	return true;
}


bool aeadHeaderWrite(Aead *this)
{
    /* Configure the cipher parameters. */
    if (!aeadCipherSetup(this, this->cipherName))
		return false;

    /* Generate an initialization vector. */
    RAND_bytes(this->iv, (int)this->ivSize);

    /* Declare a local buffer to hold the header we're creating */
    Byte header[MAX_AEAD_HEADER_SIZE];
    Byte *bp = header;
    Byte *end = header + sizeof(header);

    /* Plaintext record size for this file. */
	this->plainSize = this->suggestedSize;
    pack4(&bp, end, this->plainSize);

    /* Cipher name */
    pack1(&bp, end, strlen(this->cipherName));
    packBytes(&bp, end, (Byte *)this->cipherName, strlen(this->cipherName));

    /* Initialization vector */
    pack1(&bp, end, this->ivSize);
    packBytes(&bp, end, this->iv, this->ivSize);

    /* Verify we haven't overflowed our buffer. */
    if (bp > end)
        return setIoStackError(this, "Trying to write a header which is too large");

    /* Encrypt an empty plaintext block and authenticate the header. */
    Byte emptyCiphertext[EVP_MAX_BLOCK_LENGTH];
    Byte emptyPlaintext[0];
    Byte tag[EVP_MAX_MD_SIZE];
    size_t emptyCipherSize = aead_encrypt(this, emptyPlaintext, 0, header, end-header,
			 emptyCiphertext, sizeof(emptyCiphertext), tag, HEADER_SEQUENCE_NUMBER);
    if (emptyCipherSize != paddingSize(this, 0) || emptyCipherSize > 256)
        return setIoStackError(this, "Size of cipher padding for empty record was miscalculated");

    /* Add the empty block and tag to the header */
    pack1(&bp, end, emptyCipherSize);
    packBytes(&bp, end, emptyCiphertext, emptyCipherSize);
    pack1(&bp, end, this->tagSize);
    packBytes(&bp, end, tag, this->tagSize);

    /* Verify we haven't overflowed the header */
    if (bp+this->tagSize >= end)
        return setIoStackError(this, "Encryption file header was too large.");

    /* Write the header to the output file */
    if (fileWriteSized(nextStack(this), header, bp - header, 0, this->ctx) <=  0)
		return setNextError(this, false);

    /* Remember the header size. Since we did a "sized" write, add 4 bytes for the record size. */
    this->headerSize = bp - header + 4;
	this->cryptSize = cryptSize(this, this->plainSize);
	return true;
}


/**
 * Helper function to get the cipher details.
 * TODO: move the encryption fields into a substructure of Aead, so this code doesn't need to know about IoStacks.
 */
bool aeadCipherSetup(Aead *this, char *cipherName)
{
    /* Save the cipher name. The name must be an exact match to a libcrypto name. */
    /* TODO KLUDGE: The cipher name is already in this->cipherName. Don't copy if already there ... FIX IT!  */
    if (this->cipherName != cipherName) /* comparing pointers */
        strlcpy(this->cipherName, cipherName, sizeof(this->cipherName));

    /* Create an OpenSSL cipher context. */
    this->ctx = EVP_CIPHER_CTX_new();
	if (this->ctx == NULL)
		return setOpenSSLError(this, false);

    /* Lookup cipher by name. */
    this->cipher = EVP_CIPHER_fetch(NULL, this->cipherName, NULL);
    if (this->cipher == NULL)
        return setIoStackError(this, "Encryption problem - cipher name %w not recognized", this->cipherName);

    /* Verify cipher is an AEAD cipher */
    /* TODO: should be possible */

    /* Get the properties of the selected cipher */
    this->ivSize = EVP_CIPHER_iv_length(this->cipher);
    if (this->keySize != EVP_CIPHER_key_length(this->cipher))
        return setIoStackError(this, "Cipher key is the wrong size");
    this->cipherBlockSize = EVP_CIPHER_block_size(this->cipher);
    this->hasPadding = (this->cipherBlockSize != 1);
    this->tagSize = 16;  /* TODO: EVP_CIPHER_CTX_get_tag_length(this->ctx); But only after initialized. */

	return true;
}


/*
 * Calculate how much padding is added when encrypting a record.
 */
size_t paddingSize(Aead *this, size_t recordSize)
{
    return (this->hasPadding)
       ? this->cipherBlockSize - (recordSize % this->cipherBlockSize)
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
 *  @param cryptSize - the size of the buffer on input, actual size on output.
 *  @param tag - the output MAC tag of size this->tagSize
 *  @param blockNr - used to generate nonce
 *  @return - the actual size of the encrypted ciphertext or -1 on error.
 */
ssize_t
aead_encrypt(Aead *this,
             const Byte *plainText, size_t plainSize,
             Byte *header, size_t headerSize,
             Byte *cipherText, size_t cipherSize,
             Byte *tag, ssize_t blockNr)
{

    //debug("Encrypt: plainText='%.*s' plainSize=%zu  cipher=%s\n", (int)sizeMin(plainSize,64), plainText, plainSize, this->cipherName);
    debug("Encrypt: plainSize=%zu  cipher=%s plainText='%.*s'\n",
          plainSize, this->cipherName, (int)plainSize, plainText);
    /* Reinitialize the encryption context to start a new record */
    EVP_CIPHER_CTX_reset(this->ctx);

    /* Generate nonce by XOR'ing the initialization vector with the sequence number */
    Byte nonce[EVP_MAX_IV_LENGTH];
    generateNonce(nonce, this->iv, this->ivSize, blockNr);
    debug("Encrypt: iv=%s  blockNr=%zu  nonce=%s  key=%s\n",
          asHex(this->iv, this->ivSize), blockNr, asHex(nonce, this->ivSize), asHex(this->key, this->keySize));

    /* Configure the cipher with the key and nonce */
    if (!EVP_CipherInit_ex2(this->ctx, this->cipher, this->key, nonce, 1, NULL))
        return setOpenSSLError(this, -1);

    /* Include the header, if any, in the digest */
    if (headerSize > 0)
    {
        int zero = 0;
        if (!EVP_CipherUpdate(this->ctx, NULL, &zero, header, (int)headerSize))
            return setOpenSSLError(this, false);
    }

    /* Encrypt the plaintext if any. */
    int cipherUpdateSize = 0;
    if (plainSize > 0)
    {
        cipherUpdateSize = (int)cipherSize;
        if (!EVP_CipherUpdate(this->ctx, (Byte *)cipherText, &cipherUpdateSize, plainText, (int)plainSize))
            return setOpenSSLError(this, -1);
    }

    /* Finalise the plaintext encryption. This can generate data, usually padding, even if there is no plain text. */
    int cipherFinalSize = (int)cipherSize - cipherUpdateSize;
    if (!EVP_CipherFinal_ex(this->ctx, (Byte *)cipherText + cipherUpdateSize, &cipherFinalSize))
        return setOpenSSLError(this, -1);

    /* Get the authentication tag  */
    if (!EVP_CIPHER_CTX_ctrl(this->ctx, EVP_CTRL_AEAD_GET_TAG, (int)this->tagSize, tag))
        return setOpenSSLError(this, -1);

    debug("Encrypt: tag=%s cryptSize=%d cipherText=%.128s \n", asHex(tag, this->tagSize), cipherUpdateSize+cipherFinalSize, asHex(cipherText, cipherUpdateSize + cipherFinalSize));
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
 *  @param cryptSize - the size of the buffer on input, actual size on output.
 *  @param tag - the output MAC tag of size this->tagSize
 *  @param error - Keep track of errors.
 */
ssize_t
aead_decrypt(Aead *this,
             Byte *plainText, size_t plainSize,
             Byte *header, size_t headerSize,
             Byte *cipherText, size_t cipherSize,
             Byte *tag, ssize_t blockNr)
{
    debug("Decrypt:  cryptSize=%zu  cipher=%s  cipherText=%.128s \n", cipherSize, this->cipherName,  asHex(cipherText, cipherSize));
    /* Reinitialize the encryption context to start a new record */
    EVP_CIPHER_CTX_reset(this->ctx);

    /* Generate nonce by XOR'ing the initialization vector with the sequence number */
    Byte nonce[EVP_MAX_IV_LENGTH];
    generateNonce(nonce, this->iv, this->ivSize, blockNr);
    debug("Decrypt: iv=%s  blockNr=%zu  nonce=%s  key=%s  tag=%s\n",
          asHex(this->iv, this->ivSize), blockNr, asHex(nonce, this->ivSize), asHex(this->key, this->keySize), asHex(tag, this->tagSize));

    /* Configure the cipher with key and initialization vector */
    if (!EVP_CipherInit_ex2(this->ctx, this->cipher, this->key, nonce, 0, NULL))
        return setOpenSSLError(this, -1);

    /* Set the MAC tag we need to match */
    if (!EVP_CIPHER_CTX_ctrl(this->ctx, EVP_CTRL_AEAD_SET_TAG, (int)this->tagSize, tag))
        return setOpenSSLError(this, -1);

    /* Include the header, if any, in the digest */
    if (headerSize > 0)
    {
        int zero = 0;
        if (!EVP_CipherUpdate(this->ctx, NULL, &zero, header, (int)headerSize))
            return setOpenSSLError(this, -1);
    }

    /* Decrypt the body if any. We have two pieces: update and final. */
    int plainUpdateSize = 0;
    if (cipherSize > 0)
    {
        plainUpdateSize = (int)plainSize;
        if (!EVP_CipherUpdate(this->ctx, plainText, &plainUpdateSize, cipherText, (int)cipherSize))
            return setOpenSSLError(this, -1);
    }

    /* Finalise the decryption. This can, but probably won't, generate plaintext. */
    int plainFinalSize = (int)plainSize - plainUpdateSize;
    if (!EVP_CipherFinal_ex(this->ctx, plainText + plainUpdateSize, &plainFinalSize) && ERR_get_error() != 0)
        return setOpenSSLError(this, -1);

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
static ssize_t setOpenSSLError(Aead *this, ssize_t ret)
{
	int code = ERR_get_error();
	char *msg = ERR_error_string(code, NULL);
    setIoStackError(this, "OpenSSL error: (%d) %s", code, msg);

    return ret;
}
