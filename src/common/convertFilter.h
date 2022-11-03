/* */
/* Created by John Morris on 10/18/22. */
/* */

#ifndef  Stage_CONVERT Stage_H
#define  Stage_CONVERT Stage_H

#include "common/stage.h"
#include "common/filter.h"

Stage *convertFilterNew(size_t blockSize, Filter *writer, Filter* reader, Stage *next);

#endif /*FILTER_CONVERTFILTER_H */
