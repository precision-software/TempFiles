//
// Created by John Morris on 10/10/22.
//

#ifndef UNTITLED1_PASSTHROUGH_H
#define UNTITLED1_PASSTHROUGH_H
#include "filter.h"

extern FilterInterface passThroughInterface;
typedef Filter PassThroughFilter;

void passThroughOpen(void *this, OpenRequest *req);
void passThroughRead(void *this, ReadRequest *req);
void passThroughWrite(void *this, WriteRequest *req);
void passThroughSeek(void *this, SeekRequest *req);
void passThroughSync(void *this, SyncRequest *req);
void passThroughClose(void *this, CloseRequest *req);
void passThroughAbort(void *this, AbortRequest *req);
void passThroughPeek(void *this, PeekRequest *req);

// Helper function to ensure all the data is written.
void passThroughWriteAll(void *this, WriteRequest *req);
void passThroughReadAll(void *this, ReadRequest *req);

#endif //UNTITLED1_PASSTHROUGH_H
