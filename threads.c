#include "threads.h"
#include "sort_index.h"
#include "gen.h"

extern pthread_barrier_t barrier;
extern int* blockMap;
extern pthread_mutex_t* mutex;
extern int numOfBlocks;
extern volatile int bufferIsReady;
extern void* buffer;
volatile int iterator;

//сортировка слиянием
void mergeSortedBlocks(Index* arr1, int size1, Index* arr2, int size2, Index* result) {
    int index1 = 0, index2 = 0, resultIndex = 0;

    //попарно сравниваем элементы, пока не дойдем до конца
    while (index1 < size1 && index2 < size2) {
        if (arr1[index1].timeMark <= arr2[index2].timeMark) {
            result[resultIndex] = arr1[index1];
            resultIndex++;
            index1++;
        }
        else {
            result[resultIndex] = arr2[index2];
            resultIndex++;
            index2++;
        }
    }

    //добавляем оставшиеся элементы из первого массива
    while (index1 < size1) {
        result[resultIndex] = arr1[index1];
        resultIndex++;
        index1++;
    }

    //добавляем оставшиеся элементы из второго массива
    while (index2 < size2) {
        result[resultIndex] = arr2[index2];
        resultIndex++;
        index2++;
    }
}

int compareRecords(const void* rec1, const void* rec2) {
    Index* index1 = (Index*)rec1;
    Index* index2 = (Index*)rec2;
    return (int)(index1->timeMark - index2->timeMark);
}

void* startThread(void* info) {
    ThreadInfo* threadInfo = (ThreadInfo*)info;
    //ожидание, пока главный поток обновит буфер
    while(1) {
        //завершить текущий поток, если был запрос на отмену потока из главного потока
        pthread_testcancel();
        if (bufferIsReady) {
            //ожидание готовности остальных потоков
            pthread_barrier_wait(&barrier);
            sortingPhase(threadInfo);
        }
    }
    return NULL;
}

int findFreeBlockInMap() {
    for (int i = 0; i < numOfBlocks; i++) {
        if (blockMap[i] == 1)
            return i;
    }
    return -1;
}

int findNotMergedPair() {
    for (int i = 0; i < numOfBlocks / (iterator * 2); i++) {
        if (blockMap[i] == 0) {
            return i;
        }
    }
    return -1;
}

void sortingPhase(ThreadInfo* threadInfo) {
    //адрес блока, который будет сортировать данный поток
    Index* startAddress = (Index*)((char*)buffer + threadInfo->threadNum * threadInfo->blockSize);
    //количество записей в одном блоке
    int numOfRecordsToSort = threadInfo->blockSize / sizeof(Index);
    //сортировка блока
    qsort(startAddress, numOfRecordsToSort, sizeof(Index), compareRecords);
    while(1) {
        int retVal = pthread_mutex_lock(mutex);
        if (retVal != 0) {
            printf("Error locking mutex in thread %d (%lu): %s\n", threadInfo->threadNum, pthread_self(), strerror(retVal));
            exit(retVal);
        }
        int indexOfFreeBlock = findFreeBlockInMap();
        //если все записи заняты, то перейти к синхронизации на барьере
        if (indexOfFreeBlock == -1) {
            retVal = pthread_mutex_unlock(mutex);
            if (retVal != 0) {
                printf("Error unlocking mutex in thread %d (%lu): %s\n", threadInfo->threadNum, pthread_self(),strerror(retVal));
                exit(retVal);
            }
            break;
        }
        //отметить блок как занятый
        blockMap[indexOfFreeBlock] = 0;
        retVal = pthread_mutex_unlock(mutex);
        if (retVal != 0) {
            printf("Error unlocking mutex in thread %d (%lu): %s\n", threadInfo->threadNum, pthread_self(),strerror(retVal));
            exit(retVal);
        }
        startAddress = (Index*)((char*)buffer + indexOfFreeBlock * threadInfo->blockSize);
        qsort(startAddress, numOfRecordsToSort, sizeof(Index), compareRecords);
    }
    //ожидание завершения сортировки всех блоков
    pthread_barrier_wait(&barrier);
    mergingPhase(threadInfo);
}

void mergingPhase(ThreadInfo* threadInfo) {
    iterator = 1;
    Index *block1;
    Index *block2;
    //адреса блоков, которые будет сливать данный поток
    block1 = (Index*)((char*)buffer + threadInfo->threadNum * 2 * threadInfo->blockSize);
    block2 = (Index*)((char*)buffer + (threadInfo->threadNum * 2 + 1) * threadInfo->blockSize);
    int retVal = pthread_mutex_lock(mutex);
    if (retVal != 0) {
        printf("Error locking mutex in thread %d (%lu): %s\n", threadInfo->threadNum, pthread_self(),
               strerror(retVal));
        exit(retVal);
    }
    //устанавливаем текущую пару как слитую
    blockMap[threadInfo->threadNum] = 1;
    retVal = pthread_mutex_unlock(mutex);
    if (retVal != 0) {
        printf("Error unlocking mutex in thread %d (%lu): %s\n", threadInfo->threadNum, pthread_self(),
               strerror(retVal));
        exit(retVal);
    }
    //количество индексов в блоке
    int numOfIndexesPerBlock = threadInfo->blockSize / sizeof(Index);
    //массив для временного хранения 2 слитых блоков
    Index* mergingResult = (Index*)calloc(numOfIndexesPerBlock * 2, sizeof(Index));
    //слияние 2 блоков
    mergeSortedBlocks(block1, numOfIndexesPerBlock, block2, numOfIndexesPerBlock, mergingResult);
    //копируем слитые блоки обратно в файл
    memcpy((char*)buffer + threadInfo->threadNum * 2 * threadInfo->blockSize, mergingResult, threadInfo->blockSize * 2);
    free(mergingResult);
    pthread_barrier_wait(&barrier);
    //пока больше 2 блоков
    while(numOfBlocks / iterator > 2) {
        //сливаем все блоки в текущей итерации
        while(1) {
            retVal = pthread_mutex_lock(mutex);
            if (retVal != 0) {
                printf("Error locking mutex in thread %d (%lu): %s\n", threadInfo->threadNum, pthread_self(),
                       strerror(retVal));
                exit(retVal);
            }
            int indexOfNotMergedPair = findNotMergedPair();
            //если нет пар для слияния, то перейти к синхронизации на барьере
            if (indexOfNotMergedPair == -1) {
                retVal = pthread_mutex_unlock(mutex);
                if (retVal != 0) {
                    printf("Error unlocking mutex in thread %d (%lu): %s\n", threadInfo->threadNum, pthread_self(),
                           strerror(retVal));
                    exit(retVal);
                }
                break;
            }
            //отметить пару как слитую
            blockMap[indexOfNotMergedPair] = 1;
            retVal = pthread_mutex_unlock(mutex);
            if (retVal != 0) {
                printf("Error unlocking mutex in thread %d (%lu): %s\n", threadInfo->threadNum, pthread_self(),
                       strerror(retVal));
                exit(retVal);
            }
            //адреса блоков, которые будет сливать данный поток
            block1 = (Index*)((char*)buffer + indexOfNotMergedPair * 2 * (threadInfo->blockSize * iterator));
            block2 = (Index*)((char*)buffer + (indexOfNotMergedPair * 2 + 1) * (threadInfo->blockSize * iterator));
            //количество индексов в блоке
            numOfIndexesPerBlock = (threadInfo->blockSize * iterator) / sizeof(Index);
            //массив для временного хранения 2 слитых блоков
            Index* mergedBlocks = (Index*)calloc(numOfIndexesPerBlock * 2, sizeof(Index));
            //слияние 2 блоков
            mergeSortedBlocks(block1, numOfIndexesPerBlock, block2, numOfIndexesPerBlock, mergedBlocks);
            //копируем слитые блоки обратно в файл
            memcpy((char*)buffer + indexOfNotMergedPair * 2 * (threadInfo->blockSize * iterator),mergedBlocks,threadInfo->blockSize * 2 * iterator);
            free(mergedBlocks);
        }
        pthread_barrier_wait(&barrier);
        if (threadInfo->threadNum == 0) {
            //увеличить размер блока в 2 раза
            iterator *= 2;
            //установить блоки как не слитые
            for (int i = 0; i < numOfBlocks / iterator; i++) {
                blockMap[i] = 0;
            }
        }
        //ожидание слияния всех блоков в текущей итерации
        pthread_barrier_wait(&barrier);
    }
    //когда осталось 2 блока
    //главный поток сливает оставшиеся блоки
    if (threadInfo->threadNum == 0) {
        //адреса двух оставшихся блоков, которые будет сливать главный поток
        block1 = (Index*)buffer;
        block2 = (Index*)((char*)buffer + threadInfo->blockSize * iterator);
        //количество индексов в блоке
        numOfIndexesPerBlock = (threadInfo->blockSize * iterator) / sizeof(Index);
        //массив для временного хранения 2 слитых блоков
        mergingResult = (Index*)calloc(numOfIndexesPerBlock * 2, sizeof(Index));
        //слияние 2 блоков
        mergeSortedBlocks(block1, numOfIndexesPerBlock, block2, numOfIndexesPerBlock, mergingResult);
        //копируем слитые блоки обратно в файл
        memcpy(buffer,mergingResult,threadInfo->blockSize * 2 * iterator);
        bufferIsReady = 0;
        free(mergingResult);
    }
    pthread_barrier_wait(&barrier);
}