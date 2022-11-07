


## Use Cases
### fread/fwrite replacement
```mermaid
flowchart LR
    source[FileSource<hr>fileRead<br>fileWrite <br> fileOpen <br> fileClose <br> fileSync]
       <-- bytes --> BufferedStream <-- blocks -->
    sink[FileSystemSink <hr> read <br> write <br> open <br> close <br> datasync]
```

### Encryption
```mermaid
flowchart LR
    source[FileSource <hr> fileRead <br> fileWrite <br> fileOpen <br> fileClose <br> fileSync]
       <-- bytes --> BufferedStream <-- blocks --> Encryption <-- blocks --> 
    sink[FileSystemSink <hr> read <br> write <br> open <br> close <br> datasync]
```

### Compression
```mermaid
flowchart LR
    source[FileSource <hr> fileRead <br> fileWrite <br> fileOpen <br> fileClose <br> fileSync]
       <-- bytes --> BufferedStream <-- blocks --> LZ4Compression <-- blocks --> 
    sink[FileSystemSink <hr> read <br> write <br> open <br> close <br> datasync]
```

### Split a stream into multiple files.
```mermaid

flowchart LR
    source[FileSource <hr> fileRead <br> fileWrite <br> fileOpen <br> fileClose <br> fileSync]
       <-- bytes --> BufferedStream <-- blocks --> a[File Split <hr> multiple <br> files] <-- blocks --> 
    sink[FileSystemSink <hr> read <br> write <br> open <br> close <br> datasync]
```

### Why not? All of the above.
```mermaid
flowchart LR
    source[FileSource <hr> fileRead <br> fileWrite <br> fileOpen <br> fileClose <br> fileSync]
       <-- bytes --> BufferedStream  <-- blocks --> LZ4Compression <-- blocks --> Encryption <-- blocks --> a[File Split <hr> multiple <br> files] <-- blocks --> 
    sink[FileSystemSink <hr> read <br> write <br> open <br> close <br> datasync]
```

TODO:
- checksum/digest
- bring code in line with Postgres standards
- non-static error messages
- enforce readable/writeable in read/write
- O_DIRECT and async I/O?
- add isReadable, isWriteable, isOpen to header, so passThroughXXX can do some simple error handling (instead of each filter)
  Stream API
