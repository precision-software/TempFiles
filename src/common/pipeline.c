//
// Created by John Morris on 10/24/22.
//
#include <stdarg.h>
#include "pipeline.h"
#define countof(array) sizeof(array)/sizeof(*array)
static const int MAX_FILTERS = 256;

Filter *pipelineNew(size_t minRead, size_t maxWrite, ...)
{
    Filter *filters[MAX_FILTERS];
    va_list argp;

    // Build up a list of filters.
    size_t nrFilters;
    va_start(argp, maxWrite);
    for (nrFilters=0; nrFilters<MAX_FILTERS; nrFilters++)
    {
        filters[nrFilters] = va_arg(argp, Filter*);
        if (filters[nrFilters] == NULL)
            break;
    }
    va_end(argp);

    // Scanning left to right, update the min Read Size.
    for (int i = 0; i < nrFilters; i++)
    {
        filters[i]->minRead = minRead;
        minRead = filterReadSize(filters[i], minRead);
    }

    // Scanning right to left, update the max Read Size.
    for (int i = nrFilters-1; i >= 0; i--)
    {
        filters[i]->maxWrite = maxWrite;
        maxWrite = filterWriteSize(filters[i], maxWrite);
    }

    // Scanning right to left, fill in the "next" pointers.
    Filter *successor = &invalidFilter;
    for (int i=nrFilters-1; i >= 0; i--)
    {
        Filter *filter = filters[i];
        filter->next = successor;
        if (filter->iface->fnOpen == NULL)
            filter->nextOpen = successor->nextOpen;
        if (filter->iface->fnOpen == NULL)
            filter->nextRead = successor->nextRead;
        if (filter->iface->fnOpen == NULL)
            filter->nextWrite = successor->nextWrite;
        if (filter->iface->fnOpen == NULL)
            filter->nextClose = successor->nextClose;
        if (filter->iface->fnOpen == NULL)
            filter->nextSync = successor->nextSync;
        if (filter->iface->abort == NULL)
            filter->nextAbort = successor->nextAbort;
        successor = filter;
    }

    // Return the front of the pipeline.
    return filters[0];
}
