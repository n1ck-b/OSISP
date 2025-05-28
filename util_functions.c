#include "util_functions.h"

extern FSInfo fsInfo;
extern FsState state;
extern unsigned short FAT[MAX_CLUSTERS];

DirEntry* readAllEntries() {
    DirEntry* dirEntries = (DirEntry*)calloc(1024, sizeof(DirEntry));
    //перемещаем указатель внутри "контейнера" от его начала на 1 кластер(где находится FSInfo) + размер FAT
    if (fseek(state.container, fsInfo.sizeOfClusterInBytes + fsInfo.sizeOfFatInBytes, SEEK_SET) != 0) {
        free(dirEntries);
        return NULL;
    }
    if (fread(dirEntries, sizeof(DirEntry), 1024, state.container) < 1024) {
        free(dirEntries);
        return NULL;
    }
    return dirEntries;
}

int splitPath(char* path, char*** components, int* depth) {
    int counter = 0;
    char* temp = strtok(path, "/");
    //если не найдено лексем
    if (temp == NULL) {
        strcpy(*components[counter], "/");
        *depth = 0;
        return 0;
    }
    if (strlen(temp) > 255) {
        return -1;
    }
    *components[counter] = temp;
    counter++;
    while(counter <= MAX_DEPTH && temp != NULL) {
        temp = strtok(NULL, "/");
        //если длина лексемы > 255 символов
        if (temp != NULL && strlen(temp) > 255) {
            *depth = counter - 1;
            return -1;
        }
        //если была найдена лексема
        if (temp != NULL) {
            strcpy((*components)[counter], temp);
            counter++;
        }
    }
    *depth = counter - 1;
    return 0;
}

int findDir(char* dirName, DirEntry* entries, int* index, int numOfDirs) {
    for(int i = 0; i < numOfDirs; i++) {
        //если совпадают имена и dirEntry - каталог
        if (strncmp(entries[i].name, dirName, strlen(dirName)) == 0 && entries[i].attributes == 0x10) {
            *index = i;
            return 1;
        }
    }
    return 0;
}

unsigned char* readCluster(unsigned numOfCluster) {
    unsigned char* buffer = (unsigned char*)calloc(fsInfo.sizeOfClusterInBytes, sizeof(unsigned char));
    //смещение до нужного нам кластера: байт, с которого начинается блок данных + номер кластера * размер одного кластера
    unsigned offset = fsInfo.startOfData + numOfCluster * fsInfo.sizeOfClusterInBytes;
    if (fseek(state.container, offset, SEEK_SET) != 0) {
        free(buffer);
        return NULL;
    }
    if (fread(buffer, sizeof(unsigned char), fsInfo.sizeOfClusterInBytes, state.container) < fsInfo.sizeOfClusterInBytes) {
        free(buffer);
        return NULL;
    }
    return buffer;
}

unsigned char* readClusters(unsigned short numOfFirstCluster, int* numOfReadClusters) {
    //номер текущего кластера
    unsigned short currentCluster = numOfFirstCluster;
    unsigned offset = 0;
    //количество считанных кластеров
    int counter = 0;
    //буфер для считывания данных
    unsigned char* buffer = NULL;
    //читаем кластеры, пока не встретим значение 0xFFFF в FAT
    while(currentCluster != 0xFFFF) {
        unsigned char* temp = readCluster(currentCluster);
        //если кластер не был прочитан
        if (temp == NULL) {
            if(buffer != NULL)
                free(buffer);
            return NULL;
        }
        counter++;
        buffer = (unsigned char*)realloc(buffer, fsInfo.sizeOfClusterInBytes * counter);
        //копируем считанные данные в буфер
        memcpy(buffer + offset, temp, fsInfo.sizeOfClusterInBytes);
        offset += fsInfo.sizeOfClusterInBytes;
        free(temp);
        currentCluster = FAT[currentCluster];
    }
    *numOfReadClusters = counter;
    return buffer;
}

DirEntry* convertToDirEntries(unsigned char* buffer, int numOfEntries, int numOfClusters) {
    DirEntry* dirEntries = (DirEntry*)calloc(numOfEntries, sizeof(DirEntry));
    int counter = 0;
    //проходим по всем кластерам в буфере
    for(int i = 0; i < numOfClusters; i++) {
        //проходим по каждому dirEntry в текущем кластере, игнорируя лишние байты
        for(int j = 0; j < fsInfo.sizeOfClusterInBytes / sizeof(DirEntry); j++) {
            DirEntry *temp = (DirEntry *) (buffer + (i * fsInfo.sizeOfClusterInBytes) + (j * sizeof(DirEntry)));
            memcpy(dirEntries + counter, temp, sizeof(DirEntry));
            counter++;
        }
    }
    return dirEntries;
}

unsigned short findLastCluster(unsigned short numOfFirstCluster) {
    unsigned short currentCluster = numOfFirstCluster;
    unsigned short lastCluster;
    while(currentCluster != 0xFFFF) {
        lastCluster = currentCluster;
        currentCluster = FAT[currentCluster];
    }
    return lastCluster;
}

unsigned short checkClusterForEmptySlots(unsigned char* buffer) {
    unsigned short counter = 0;
    unsigned short numOfEntries = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
    DirEntry* entries = convertToDirEntries(buffer, numOfEntries, 1);
    for (int i = 0; i < numOfEntries; i++) {
        if ((unsigned char)entries[i].name[0] == 0x00 || (unsigned char)entries[i].name[0] == 0xE5) {
            counter++;
        }
    }
    free(entries);
    return counter;
}

int findFirstFreeCluster(unsigned short numOfClusterToTry) {
    if (FAT[numOfClusterToTry] == 0x00) {
        return numOfClusterToTry;
    } else {
        for(int i = 0; i < MAX_CLUSTERS; i++) {
            if (FAT[i] == 0x00) {
                return i;
            }
        }
    }
    return -1;
}

int allocateNewCluster(int numOfLatestCluster, int isDir) {
    if (fsInfo.numOfFreeClusters == 0) {
        return -1;
    }
    unsigned short newCluster = fsInfo.numOfFirstFreeCluster;
    short tmp = findFirstFreeCluster(fsInfo.numOfFirstFreeCluster + 1);
    if (tmp == -1) {
        return -1;
    }
    fsInfo.numOfFirstFreeCluster = tmp;
    fsInfo.numOfFreeClusters--;
    if (numOfLatestCluster != -1)
        FAT[numOfLatestCluster] = newCluster;
    FAT[newCluster] = 0xFFFF;
    if (fseek(state.container, fsInfo.startOfData + newCluster * fsInfo.sizeOfClusterInBytes, SEEK_SET) != 0) {
        return -2;
    }
    if (!isDir) {
        unsigned char* emptyBytes = (unsigned char*)calloc(fsInfo.sizeOfClusterInBytes, sizeof(unsigned char));
        if (fwrite(emptyBytes, sizeof(unsigned char), fsInfo.sizeOfClusterInBytes, state.container) < fsInfo.sizeOfClusterInBytes) {
            free(emptyBytes);
            return -2;
        }
        free(emptyBytes);
    } else {
        unsigned short numOfEntries = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        DirEntry* emptyEntries = (DirEntry*)calloc(numOfEntries, sizeof(DirEntry));
        if (fwrite(emptyEntries, sizeof(DirEntry), numOfEntries, state.container) < numOfEntries) {
            free(emptyEntries);
            return -2;
        }
        free(emptyEntries);
    }
    return newCluster;
}

unsigned checkForExistenceOfFileOrDirectory(DirEntry* entries, unsigned short numOfEntries, char* entryName, unsigned char attr) {
    for (int i = 0; i < numOfEntries; i++) {
        if (strcmp(entries[i].name, entryName) == 0 && entries[i].attributes == attr) {
            return 1;
        }
    }
    return 0;
}

int writeDirEntriesToCluster(unsigned short numOfCluster, DirEntry* entries) {
    unsigned short numOfEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
    //смещение до нужного нам кластера: байт, с которого начинается блок данных + номер кластера * размер одного кластера
    unsigned offset = fsInfo.startOfData + numOfCluster * fsInfo.sizeOfClusterInBytes;
    if (fseek(state.container, offset, SEEK_SET) != 0) {
        return -1;
    }
    if (fwrite(entries, sizeof(DirEntry), numOfEntriesPerCluster, state.container) < numOfEntriesPerCluster) {
        return -1;
    }
    return 0;
}

int writeDirEntriesToEmptyCluster(unsigned short clusterNum, DirEntry* entries) {
    unsigned char* buffer = readCluster(clusterNum);
    unsigned short numOfEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
    DirEntry* readEntries = convertToDirEntries(buffer, numOfEntriesPerCluster, 1);
    memcpy(readEntries, entries, sizeof(DirEntry));
    memcpy(readEntries + 1, entries + 1, sizeof(DirEntry));
    //смещение до нужного нам кластера: байт, с которого начинается блок данных + номер кластера * размер одного кластера
    unsigned offset = fsInfo.startOfData + clusterNum * fsInfo.sizeOfClusterInBytes;
    if (fseek(state.container, offset, SEEK_SET) != 0) {
        free(buffer);
        free(readEntries);
        return -1;
    }
    if (fwrite(readEntries, sizeof(DirEntry), numOfEntriesPerCluster, state.container) < numOfEntriesPerCluster) {
        free(buffer);
        free(readEntries);
        return -1;
    }
    free(buffer);
    free(readEntries);
    return 0;
}

unsigned countSubDirs(DirEntry* entries, unsigned short numOfEntries) {
    unsigned counter = 0;
    for (int i = 0; i < numOfEntries; i++) {
        if (entries[i].attributes == 0x10 && (unsigned char)entries[i].name[0] != 0x00 && (unsigned char)entries[i].name[0] != 0xE5 && strcmp(entries[i].name, ".") != 0) {
            counter++;
        }
    }
    return counter;
}

struct timespec convertTimeToTimespec(time_t time) {
    struct timespec tm;
    tm.tv_sec = time;
    tm.tv_nsec = 0;
    return tm;
}

unsigned short countClustersInChain(unsigned short startCluster) {
    unsigned short currentCluster = startCluster;
    unsigned short counter = 0;
    while (currentCluster != 0xFFFF) {
        counter++;
        currentCluster = FAT[currentCluster];
    }
    return counter;
}

int writeBytesToFile(unsigned char startCluster, unsigned char* buffer) {
    unsigned short numOfClustersToWrite = ceil(sizeof(buffer) / (double)fsInfo.sizeOfClusterInBytes);
    unsigned short currentCluster = startCluster;
    unsigned offset = 0;
    for (int i = 0; i < numOfClustersToWrite; i++) {
        if (fseek(state.container, fsInfo.startOfData + currentCluster * fsInfo.sizeOfClusterInBytes, SEEK_SET) != 0) {
            return -1;
        }
        if (fwrite(buffer + offset, sizeof(unsigned char), fsInfo.sizeOfClusterInBytes, state.container) < fsInfo.sizeOfClusterInBytes) {
            return -1;
        }
        currentCluster = FAT[currentCluster];
        offset += fsInfo.sizeOfClusterInBytes;
    }
    return 0;
}

int rewriteDirEntries(unsigned short startCluster, DirEntry* entries, unsigned short numOfEntries) {
    unsigned short numOfEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
    unsigned short currentCluster = startCluster;
    unsigned offset = 0;
    for (int i = 0; i < numOfEntries / numOfEntriesPerCluster; i++) {
        if (fseek(state.container, fsInfo.startOfData + currentCluster * fsInfo.sizeOfClusterInBytes, SEEK_SET) != 0) {
            return -1;
        }
        if (fwrite(entries + offset, sizeof(DirEntry), numOfEntriesPerCluster, state.container) < numOfEntriesPerCluster) {
            return -1;
        }
        currentCluster = FAT[currentCluster];
        offset += numOfEntriesPerCluster;
    }
    return 0;
}

int rewriteRootDirEntries(DirEntry* entries, unsigned short numOfEntries) {
    if (fseek(state.container, fsInfo.sizeOfClusterInBytes + fsInfo.sizeOfFatInBytes, SEEK_SET) != 0) {
        return -1;
    }
    if (fwrite(entries, sizeof(DirEntry), numOfEntries, state.container) < numOfEntries) {
        return -1;
    }
    return 0;
}

unsigned short* getClustersFromChain(unsigned short firstCluster, int* counter) {
    unsigned short currentCluster = firstCluster;
    unsigned short* clustersInChain = NULL;
    *counter = 0;
    while(currentCluster != 0xFFFF) {
        (*counter)++;
        //сохраняем все кластеры в цепочке
        clustersInChain = (unsigned short*)realloc(clustersInChain, *counter * sizeof(unsigned short));
        clustersInChain[*counter - 1] = currentCluster;
        currentCluster = FAT[currentCluster];
    }
    return clustersInChain;
}

void freeClustersInChainExceptFirst(unsigned short firstCluster) {
    unsigned short* clustersInChain = NULL;
    int counter = 0;
    clustersInChain = getClustersFromChain(firstCluster, &counter);
    //если в цепочке единственный кластер
    if (counter == 1) {
        free(clustersInChain);
        return;
    }
    //освобождаем кластеры, кроме первого в цепочке
    for (int i = 1; i < counter; i++) {
        FAT[clustersInChain[i]] = 0;
        if (clustersInChain[i] < fsInfo.numOfFirstFreeCluster) {
            fsInfo.numOfFirstFreeCluster = clustersInChain[i];
        }
    }
    //на первом кластере - конец цепочки
    FAT[firstCluster] = 0xFFFF;
    fsInfo.numOfFreeClusters += counter - 1;
    free(clustersInChain);
}

unsigned short countFreeBytesInCluster(unsigned char* cluster) {
    unsigned short counter = 0;
    for (int i = 0; i < fsInfo.sizeOfClusterInBytes; i++) {
        if (cluster[i] == 0) {
            counter++;
        }
    }
    return counter;
}

int freeCluster(unsigned short numOfCluster) {
    //пустые байты для записи в кластер
    unsigned char* emptyBytes = (unsigned char*)calloc(fsInfo.sizeOfClusterInBytes, sizeof(unsigned char));
    //обнуляем кластер в контейнере
    //перемещаем указатель
    if (fseek(state.container, fsInfo.startOfData + numOfCluster * fsInfo.sizeOfClusterInBytes, SEEK_SET) != 0) {
        free(emptyBytes);
        return -1;
    }
    if (fwrite(emptyBytes, sizeof(unsigned char), fsInfo.sizeOfClusterInBytes, state.container) < fsInfo.sizeOfClusterInBytes) {
        free(emptyBytes);
        return -1;
    }
    //обнуляем кластер в FAT
    FAT[numOfCluster] = 0;
    if (numOfCluster < fsInfo.numOfFirstFreeCluster) {
        fsInfo.numOfFirstFreeCluster = numOfCluster;
    }
    fsInfo.numOfFreeClusters++;
    free(emptyBytes);
    return 0;
}

//обнулить байты с конца кластера
int zeroBytes(unsigned short numOfCluster, unsigned short numOfBytesToZero) {
    //переходим к нужному байту
    if (fseek(state.container, fsInfo.startOfData + numOfCluster * fsInfo.sizeOfClusterInBytes + (fsInfo.sizeOfClusterInBytes - numOfBytesToZero), SEEK_SET) != 0) {
        return -1;
    }
    //записываем пустые байты
    unsigned char* emptyBytes = (unsigned char*)calloc(numOfBytesToZero, sizeof(unsigned char));
    if (fwrite(emptyBytes, sizeof(unsigned char), numOfBytesToZero, state.container) < numOfBytesToZero) {
        free(emptyBytes);
        return -1;
    }
    free(emptyBytes);
    return 1;
}

time_t convertTimeFromTimespec(struct timespec tv) {
    if (tv.tv_nsec >= 500000000) {
        return tv.tv_sec + 1;
    }
    return tv.tv_sec;
}

unsigned countDirSize(unsigned short startCluster) {
    unsigned result = fsInfo.sizeOfClusterInBytes * countClustersInChain(startCluster);
    return result;
}

void freeAllClustersInChain(unsigned short firstCluster) {
    unsigned short* clustersInChain = NULL;
    int counter = 0;
    clustersInChain = getClustersFromChain(firstCluster, &counter);
    //освобождаем все кластеры в цепочке
    for (int i = 0; i < counter; i++) {
        FAT[clustersInChain[i]] = 0;
        //перезаписываем первый свободный кластер
        if (clustersInChain[i] < fsInfo.numOfFirstFreeCluster) {
            fsInfo.numOfFirstFreeCluster = clustersInChain[i];
        }
    }
    //увеличиваем число свободных кластеров
    fsInfo.numOfFreeClusters += counter;
    free(clustersInChain);
}

int freeClusterIfNeeded(int index, unsigned short startClusterOfParentDir) {
    unsigned short numOfEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
    //индекс кластера, в котором находится удаляемый файл/подкаталог, в цепочке
    unsigned short indexOfClusterInChain = index / numOfEntriesPerCluster;
    int numOfClustersInChain;
    unsigned short* clustersInChain = getClustersFromChain(startClusterOfParentDir, &numOfClustersInChain);
    //читаем кластер, в котором находится удаляемый файл/подкаталог
    unsigned char* parentDirCluster = readCluster(clustersInChain[indexOfClusterInChain]);
    if (parentDirCluster == NULL) {
        printf("Error occurred while reading from container\n");
        free(clustersInChain);
        return -1;
    }
    //если все dirEntry в кластере - пустые (или удалены)
    if (checkClusterForEmptySlots(parentDirCluster) == numOfEntriesPerCluster) {
        //если нужный нам кластер - последний в цепочке, но не единственный
        if (FAT[clustersInChain[indexOfClusterInChain]] == 0xFFFF && indexOfClusterInChain >= 1) {
            //предыдущий кластер теперь последний в цепочке
            FAT[clustersInChain[indexOfClusterInChain - 1]] = 0xFFFF;
            //отмечаем кластер как пустой
            FAT[clustersInChain[indexOfClusterInChain]] = 0;
        }
        //если нужный нам кластер - не последний в цепочке, но и не первый
        else if (FAT[clustersInChain[indexOfClusterInChain]] != 0xFFFF && indexOfClusterInChain >= 1 && numOfClustersInChain >= 3) {
            //предыдущему кластеру присваиваем номер следующего относительно того кластера, где находится удаляемый файл/подкаталог
            FAT[clustersInChain[indexOfClusterInChain - 1]] = clustersInChain[indexOfClusterInChain + 1];
            //отмечаем кластер как пустой
            FAT[clustersInChain[indexOfClusterInChain]] = 0;
        }
    }
    //если нужный кластер - первый в цепочке, но не единственный
    else if (checkClusterForEmptySlots(parentDirCluster) == numOfEntriesPerCluster - 2 && FAT[clustersInChain[indexOfClusterInChain]] != 0xFFFF && indexOfClusterInChain == 0 && numOfClustersInChain > 1) {
        //читаем данные из следующего в цепочке кластера
        unsigned char* nextCluster = readCluster(clustersInChain[indexOfClusterInChain + 1]);
        if (nextCluster == NULL) {
            free(parentDirCluster);
            free(clustersInChain);
            return -1;
        }
        //если в следующем кластере записей столько, сколько свободных записей в текущем кластере
        if (checkClusterForEmptySlots(nextCluster) >= 2) {
            DirEntry *entriesOfNextCluster = convertToDirEntries(nextCluster, numOfEntriesPerCluster, 1);
            //сортируем не пустые записи
            unsigned short numOfNotEmptyEntries = 0;
            DirEntry* notEmptyEntries = (DirEntry*)calloc(numOfEntriesPerCluster, sizeof(DirEntry));
            for (int i = 0; i < numOfEntriesPerCluster; i++) {
                if ((unsigned char)entriesOfNextCluster[i].name[0] != 0x00 && (unsigned char)entriesOfNextCluster[i].name[0] != 0xE5) {
                    notEmptyEntries[numOfNotEmptyEntries] = entriesOfNextCluster[i];
                    numOfNotEmptyEntries++;
                }
            }
            notEmptyEntries = (DirEntry*)realloc(notEmptyEntries, numOfNotEmptyEntries * sizeof(DirEntry));
            //записываем dirEntry следующего в цепочке кластера в текущий (первый в цепочке) кластер
            //переходим в нужный кластер, пропуская '.' и '..'
            if (fseek(state.container, fsInfo.startOfData + clustersInChain[indexOfClusterInChain] * fsInfo.sizeOfClusterInBytes + 2 * sizeof(DirEntry), SEEK_SET) == -1) {
                free(parentDirCluster);
                free(clustersInChain);
                free(entriesOfNextCluster);
                free(notEmptyEntries);
                free(nextCluster);
                return -1;
            }
            if (fwrite(notEmptyEntries, sizeof(DirEntry), numOfNotEmptyEntries, state.container) < numOfNotEmptyEntries) {
                free(parentDirCluster);
                free(clustersInChain);
                free(entriesOfNextCluster);
                free(notEmptyEntries);
                free(nextCluster);
                return -1;
            }
            //если следующий кластер - последний в цепочке
            if (FAT[clustersInChain[indexOfClusterInChain + 1]] == 0xFFFF) {
                //то текущий теперь последний
                FAT[clustersInChain[indexOfClusterInChain]] = 0xFFFF;
            } else {
                //иначе удаляем следующий кластер из цепочки
                FAT[clustersInChain[indexOfClusterInChain]] = FAT[clustersInChain[indexOfClusterInChain + 1]];
            }
            //следующий кластер теперь пустой
            FAT[clustersInChain[indexOfClusterInChain + 1]] = 0;
            free(entriesOfNextCluster);
        }
        free(nextCluster);
    }
    free(parentDirCluster);
    free(clustersInChain);
    return 0;
}

int saveFatToFile() {
    if (fseek(state.container, fsInfo.startOfFat, SEEK_SET) != 0) {
        return -1;
    }
    if (fwrite(FAT, sizeof(unsigned short), MAX_CLUSTERS, state.container) < MAX_CLUSTERS) {
        return -1;
    }
    return 0;
}

int saveFsInfoToFile() {
    if (fseek(state.container, 0, SEEK_SET) != 0) {
        return -1;
    }
    FSInfo* temp = (FSInfo*)calloc(1, sizeof(FSInfo));
    *temp = fsInfo;
    if (fwrite(temp, sizeof(FSInfo), 1, state.container) < 1) {
        free(temp);
        return -1;
    }
    free(temp);
    return 0;
}