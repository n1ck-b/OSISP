#include "util_functions.h"

extern FSInfo fsInfo;
extern FsState state;
extern unsigned short FAT[MAX_CLUSTERS];

DirEntry* readAllEntries() {
    printf("\nInside readAllEntries\n");
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
    printf("\n1 Inside splitPath\n");
    int counter = 0;
    char* temp = strtok(path, "/");
    //если не найдено лексем
    if (temp == NULL) {
        strcpy(*components[counter], "/");
        *depth = 0;
        return 0;
    }
    printf("\ntemp = %s\n", temp);
    *components[counter] = temp;
    printf("\n2 Inside splitPath\n");
    counter++;
    while(counter <= MAX_DEPTH && temp != NULL) {
        printf("\n5 Inside splitPath\n");
        temp = strtok(NULL, "/");
        printf("\n6 Inside splitPath\n");
        //если длина лексемы > 255 символов
        if (temp != NULL && strlen(temp) > 255) {
            *depth = counter - 1;
            return -1;
        }
        //если была найдена лексема
        if (temp != NULL) {
            printf("\n3 Inside splitPath\n");
            strcpy(*components[counter], temp);
            counter++;
        }
    }
    printf("\n4 Inside splitPath\n");
    *depth = counter - 1;
    return 0;
}

int findDir(char* dirName, DirEntry* entries, int* index, int numOfDirs) {
    printf("\nInside findDir\n");
    for(int i = 0; i < numOfDirs; i++) {
        //если совпадают имена и dirEntry - каталог
        if (strcmp(entries[i].name, dirName) == 0 && entries[i].attributes == 0x10) {
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
    if(fseek(state.container, offset, SEEK_SET) != 0) {
        free(buffer);
        return NULL;
    }
    if (fread(buffer, sizeof(unsigned char), fsInfo.sizeOfClusterInBytes, state.container) < 4096) {
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
            DirEntry *temp = (DirEntry *) (buffer + i * fsInfo.sizeOfClusterInBytes + j * sizeof(DirEntry));
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
        if (strcmp(entries[i].name, "0x00") == 0 || strcmp(entries[i].name, "0xE5") == 0) {
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
        unsigned char *empty = 0;
        if (fwrite(empty, sizeof(unsigned char), fsInfo.sizeOfClusterInBytes, state.container) < fsInfo.sizeOfClusterInBytes) {
            return -2;
        }
    } else {
        unsigned short numOfEntries = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        DirEntry emptyEntry;
        strcpy(emptyEntry.name, "0x00");
        if (fwrite(&emptyEntry, sizeof(DirEntry), numOfEntries, state.container) < numOfEntries) {
            return -2;
        }
    }
    return newCluster;
}

unsigned checkForExistenceOfFileOrDirectory(DirEntry* entries, unsigned short numOfEntries, char* entryName, unsigned char attr) {
    for(int i = 0; i < numOfEntries; i++) {
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

int countSubDirs(DirEntry* entries, unsigned short numOfEntries) {
    unsigned short counter = 0;
    for (int i = 0; i < numOfEntries; i++) {
        if (entries[i].attributes == 0x10) {
            counter++;
        }
    }
    return counter;
}

struct timespec convertTime(time_t time) {
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
        offset += numOfEntriesPerCluster * sizeof(DirEntry);
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