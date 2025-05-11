#ifndef LAB6_THREADS_H
#define LAB6_THREADS_H

#include "gen.h"

typedef struct {
    int blockSize;
    int threadNum;
    void* buffer;
} ThreadInfo;

void* startThread(void* info);
void sortingPhase(ThreadInfo* threadInfo);
void mergingPhase(ThreadInfo* threadInfo);
void mergeSortedBlocks(Index* block1, int size1, Index* block2, int size2, Index* result);
int compareRecords(const void* rec1, const void* rec2 );
#endif //LAB6_THREADS_H
