/* */
/* Created by John Morris on 11/1/22. */
/* */

#ifndef  Stage_LZ4_H
#define  Stage_LZ4_H
#include "common/stage.h"

typedef struct Lz4Stage Lz4Stage;

Stage *lz4StageNew(size_t bufferSize, Stage *next);
void lz4StageFree(void *this);

#endif /* Stage_LZ4_H */
