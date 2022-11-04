
## Use Cases

### Testing Mermaid
```mermaid
flowchart LR
   Start[fileRead <br> fileWrite <br> fileOpen <br> fileClose <br> fileSync]  
   <--> FileSource <-- bytes --> BufferedStream <-- blocks --> FilesystemSink <-->
   Stop[read <br> write <br> open <br> close <br> datasync]
```
### fread/fwrite Replacement
```plantuml
@startditaa
 fileOpen()-->+-----------+bytes+---------------+blocks+---------------+-->open()
 fileRead()-->|           |     |               |      |               |-->read()
fileWrite()-->|File Source|<--->|Buffered Stream|<---->|Filesystem Sink|-->write()
 fileSync()-->|           |     |               |      |               |-->fsync()
fileClose()-->+-----------+     +---------------+      +---------------+-->close()
@endditaa
```
### With Compression
```plantuml
@startditaa
 fileOpen()-->+-----------+bytes+---------------+blocks+---------------+blocks+---------------+-->open()
 fileRead()-->|           |     |               |      |               |      |               |-->read()
fileWrite()-->|File Source|<--->|Buffered Stream|<---->|LZ4 Compression|<---->|Filesystem Sink|-->write()
 fileSync()-->|           |     |               |      |               |      |               |-->fsync()
fileClose()-->+-----------+     +---------------+      +---------------+      +---------------+-->close()
@endditaa
```

### With Encryption
```plantuml
@startditaa
 fileOpen()-->+-----------+bytes+---------------+blocks+--------------+blocks+---------------+-->open()
 fileRead()-->|           |     |               |      |              |      |               |-->read()
fileWrite()-->|File Source|<--->|Buffered Stream|<---->|AES Encryption|<---->|Filesystem Sink|-->write()
 fileSync()-->|           |     |               |      |              |      |               |-->fsync()
fileClose()-->+-----------+     +---------------+      +--------------+      +---------------+-->close()
@endditaa
```

### Split a stream into multiple files.
```plantuml
@startditaa

 fileOpen()-->+-----------+bytes+---------------+blocks+----------+  +--\blocks+---------------+-->open()
 fileRead()-->|           |     |               |      |          +--+  |      |               |-->read()
fileWrite()-->|File Source|<--->|Buffered Stream|<---->|File Split|  :  :<---->|Filesystem Sink|-->write()
 fileSync()-->|           |     |               |      |          +--+  |      |               |-->fsync()
fileClose()-->+-----------+     +---------------+      +----------+  +--/      +---------------+-->close()
                                                                   Multiple
                                                                    Files
@endditaa

```
TODO:
- checksum/digest
- bring code in line with Postgres standards
- non-static error messages
- enforce readable/writeable in read/write
- O_DIRECT and async I/O?
- add isReadable, isWriteable, isOpen to header, so passThroughXXX can do some simple error handling (instead of each filter)
