#include "fuse_functions.h"

#define SIZE_OF_CONTAINER 269484032 //размер (257 МБ) "контейнера" в байтах, где будет размещаться файловая система

extern FSInfo fsInfo;
extern unsigned short FAT[MAX_CLUSTERS];
extern FsState state;

//инициализация файловой системы
void* sfsInit (struct fuse_conn_info *conn) {
    //заполняем структуру FSInfo
    fsInfo.sizeOfClusterInBytes = 4096;
    //максимальное число кластеров, которое мы можем адресовать в FAT * размер одной записи FAT
    fsInfo.sizeOfFatInBytes = MAX_CLUSTERS * sizeof(unsigned short);
    //начало FAT в байтах
    fsInfo.startOfFat = 4096;
    //смещение блока данных от начала файла в байтах: 1 кластер под FSInfo + размер FAT в байтах + 1024 структуры DirEntry
    fsInfo.startOfData = 4096 + fsInfo.sizeOfFatInBytes + (sizeof(DirEntry) * 1024);
    //первый свободный кластер в блоке данных
    fsInfo.numOfFirstFreeCluster = 0;
    //общее число кластеров - 1, так как он зарезервирован
    fsInfo.numOfFreeClusters = MAX_CLUSTERS - fsInfo.numOfFirstFreeCluster - 1;
    fsInfo.numOfTotalClusters = MAX_CLUSTERS;

    //заполнение всех свободных кластеров
    for(int i = fsInfo.numOfFirstFreeCluster; i < MAX_CLUSTERS - 1; i++) {
        FAT[i] = 0;
    }

    //резервируем последний кластер
    FAT[MAX_CLUSTERS - 1] = 0xFFF6;

    //получаем текущий каталог в реальной файловой системе
    char* pathToFile = calloc(PATH_MAX, sizeof(char));
    pathToFile = getcwd(pathToFile, PATH_MAX);
    strcat(pathToFile, "/sfs.img");
    //записываем в "контейнер"
    FILE *container;
    if (access(pathToFile, F_OK) == 0) {
        //если "контейнер" уже существует
        container = fopen(pathToFile, "r+b");
        if (container == NULL) {
            printf("Error occurred while opening sfs.img file\n");
            exit(EXIT_FAILURE);
        }
        //перемещение указателя на начало файла
        if (fseek(container, 0, SEEK_SET) != 0) {
            printf("Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
        FSInfo* temp = (FSInfo*)calloc(1, sizeof(FSInfo));
        if (fread(temp, sizeof(FSInfo), 1, container) < 1) {
            printf("Error occurred while initializing file system\n");
            exit(EXIT_FAILURE);
        }
        fsInfo = *temp;
        if (fseek(container, fsInfo.sizeOfClusterInBytes, SEEK_SET) != 0) {
            printf("Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
        if (fread(FAT, sizeof(unsigned short), MAX_CLUSTERS, container) < MAX_CLUSTERS) {
            printf("Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
    }
    else {
        //если "контейнер" не существует
        container = fopen(pathToFile, "w+b");
        if (container == NULL) {
            printf("Error occurred while creating sfs.img file.\n");
            exit(EXIT_FAILURE);
        }
        //записываем FsInfo
        if (fwrite(&fsInfo, sizeof(FSInfo), 1, container) < 1) {
            printf("Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
        //заполнение пустой области нулевыми байтами
        char* emptyBytes = (char*)calloc(fsInfo.sizeOfClusterInBytes - sizeof(FSInfo), sizeof(char));
        if (fwrite(emptyBytes, sizeof(char), fsInfo.sizeOfClusterInBytes - sizeof(FSInfo), container) < fsInfo.sizeOfClusterInBytes - sizeof(FSInfo)) {
            printf("Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
        free(emptyBytes);
        if (fwrite(FAT, sizeof(unsigned short), MAX_CLUSTERS, container) < MAX_CLUSTERS) {
            printf("Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
        //"пустые" dirEntry для записи в корневой каталог
        DirEntry* emptyEntries = (DirEntry*)calloc(1023, sizeof(DirEntry));
        //каталог '.' для корневого каталога
        DirEntry currentDir;
        strcpy(currentDir.name, ".");
        currentDir.permissions = S_IFDIR | 0777;
        currentDir.attributes = 0x10;
        currentDir.numOfFirstCluster = MAX_CLUSTERS;
        currentDir.sizeInBytes = 0;
        currentDir.lastAccessTime = time(NULL);
        currentDir.modificationTime = time(NULL);
        currentDir.creationTime = time(NULL);
        //запись '.' для корневого каталога
        if (fwrite(&currentDir, sizeof(DirEntry), 1, container) < 1) {
            printf("Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
        //запись пустых DirEntry в область корневого каталога
        if (fwrite(emptyEntries, sizeof(DirEntry), 1023, container) < 1023) {
            printf("Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
        emptyBytes = (char*)calloc(MAX_CLUSTERS * fsInfo.sizeOfClusterInBytes, sizeof(char));
        //заполняем область данных пустыми байтами
        if (fwrite(emptyBytes, sizeof(char), MAX_CLUSTERS * fsInfo.sizeOfClusterInBytes, container) < MAX_CLUSTERS) {
            printf("Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
        free(emptyEntries);
        free(emptyBytes);
    }
    strcpy(state.pathToContainer, pathToFile);
    state.container = container;
    free(pathToFile);
    return NULL;
}

//отключение файловой системы
void sfsDestroy(void *private_data) {
    //сохраняем измененную FAT в "контейнер"
    if (saveFatToFile() == -1) {
        printf("Error writing FAT to container\n");
    }
    //сохраняем измененную FSInfo в "контейнер"
    if (saveFsInfoToFile() == -1) {
        printf("Error writing FSInfo to container\n");
    }
    fclose(state.container);
}

//создание файла
int sfsCreate (const char* path, mode_t mode, struct fuse_file_info* fileInfo) {
    //копия пути к файлу
    char copyOfFullPath[255];
    strcpy(copyOfFullPath, path);
    //если не осталось свободных кластеров
    if (fsInfo.numOfFreeClusters == 0) {
        return -ENOSPC;
    }
    DirEntry* rootDirEntries = readAllEntries();
    //если не удалось считать dirEntries
    if (rootDirEntries == NULL) {
        printf("Error occurred while reading dirEntries.\n");
        return -EIO;
    }
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    //разбиение пути на компоненты
    if (splitPath(copyOfFullPath, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -ENAMETOOLONG;
    }
    //проходим по компонентам пути, проверяя существуют ли каталоги и считывая их
    //вложенные dirEntry относительно текущего каталога
    DirEntry latestDir;
    DirEntry* currentDirEntries = rootDirEntries; //dirEntry текущего каталога
    unsigned numOfSubDirs = 1024; //количество dirEntry текущего каталога
    for(int i = 0; i < depth; i++) {
        int index;
        //если каталог не существует
        if (!findDir(componentsOfPath[i], currentDirEntries, &index, numOfSubDirs)) {
            printf("Directory with name '%s' wasn't found.\n", componentsOfPath[i]);
            return -ENOENT;
        }
        //проверяем права на запись в текущий каталог
        if (!(currentDirEntries[index].permissions & (S_IWUSR | S_IWGRP | S_IWOTH))) { //+
            printf("Writing to the directory '%s' is prohibited.\n", currentDirEntries[index].name);
            return -EACCES;
        }
        int numOfClusters;
        //сохраняем структуру последнего в пути каталога
        latestDir = currentDirEntries[index];
        unsigned char* buffer = readClusters(currentDirEntries[index].numOfFirstCluster, &numOfClusters);
        //количество dirEntry, которые помещаются в кластер
        unsigned numOfDirEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        //приводим байты из считанных кластеров к массиву dirEntry
        currentDirEntries = convertToDirEntries(buffer, numOfDirEntriesPerCluster * numOfClusters, numOfClusters);
        numOfSubDirs = numOfDirEntriesPerCluster * numOfClusters;
    }
    //если файл создается не в корневом каталоге
    if (depth != 0) {
        //если запись в этот каталог запрещена
        if (!(latestDir.permissions & (S_IWUSR | S_IWGRP | S_IWOTH))) { //+
            printf("Writing to the directory '%s' is prohibited.\n", latestDir.name);
            return -EACCES;
        }
        //если такой файл уже существует
        if (checkForExistenceOfFileOrDirectory(currentDirEntries, numOfSubDirs, componentsOfPath[depth], 0x00)) {
            printf("File with that name already exists.\n");
            return -EEXIST;
        }
        //ищем последний кластер, принадлежащий этому каталогу
        unsigned short numOfLastCluster = findLastCluster(latestDir.numOfFirstCluster);
        unsigned char* lastCluster = readCluster(numOfLastCluster);
        unsigned short emptySlots = checkClusterForEmptySlots(lastCluster);
        //если нет свободного места в каталоге, то выделяем кластер для каталога
        if (emptySlots == 0) {
            int tmp = allocateNewCluster(numOfLastCluster, 1);
            if (tmp == -1) {
                printf("There is no free memory.\n");
                return -ENOSPC;
            } else if (tmp == -2) {
                printf("Error occurred while allocating memory\n");
                return -EIO;
            }
            numOfLastCluster = tmp;
            lastCluster = readCluster(numOfLastCluster);
        }
        //выделяем кластер для файла
        int tmp = allocateNewCluster(-1, 0);
        if (tmp == -1) {
            printf("There is no free memory.\n");
            return -ENOSPC;
        } else if (tmp == -2) {
            printf("Error occurred while allocating memory for file\n");
            return -EIO;
        }
        unsigned short numOfClusterForFile = tmp;
        unsigned short numOfEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        DirEntry* lastClusterEntries = convertToDirEntries(lastCluster, numOfEntriesPerCluster, 1);
        for (int i = 0; i < numOfEntriesPerCluster; i++) {
            if ((unsigned char)lastClusterEntries[i].name[0] == 0x00 || (unsigned char)lastClusterEntries[i].name[0] == 0xE5) {
                //заполняем информацию о файле
                strcpy(lastClusterEntries[i].name, componentsOfPath[depth]);
                lastClusterEntries[i].numOfFirstCluster = numOfClusterForFile;
                lastClusterEntries[i].permissions = mode;
                lastClusterEntries[i].attributes = 0x00;
                lastClusterEntries[i].creationTime = time(NULL);
                lastClusterEntries[i].lastAccessTime = time(NULL);
                lastClusterEntries[i].modificationTime = time(NULL);
                lastClusterEntries[i].sizeInBytes = 0;
                //сохраняем номер кластера, с которого начинается файл
                fileInfo->fh = numOfClusterForFile;
                break;
            }
        }
        //записываем обратно dirEntry, считанные из последнего кластера текущего каталога, в этот кластер
        if (writeDirEntriesToCluster(numOfLastCluster, lastClusterEntries) == -1) {
            printf("Error occurred while creating new file\n");
            return -EIO;
        }
        free(lastClusterEntries);
        //изменяем время последнего изменения для каталога, в котором находится файл
        char pathToLatestDir[255];
        strncpy(pathToLatestDir, path, strlen(path) - strlen(componentsOfPath[depth]) - 1);
        pathToLatestDir[strlen(path) - strlen(componentsOfPath[depth]) - 1] = '\0';
        struct timespec tv[2];
        tv[0].tv_nsec = UTIME_OMIT;
        tv[1] = convertTimeToTimespec(time(NULL));
        int retVal = sfsUtimens(pathToLatestDir, tv);
        if (retVal != 0) {
            return retVal;
        }
    }
    //когда файл создается в корневом каталоге
    else {
        //если такой файл уже существует
        if (checkForExistenceOfFileOrDirectory(rootDirEntries, 1024, componentsOfPath[depth], 0x00)) {
            printf("File with that name already exists.\n");
            return -EEXIST;
        }
        int numOfFirstFreeEntryInRootDir = -1;
        for (int i = 0; i < 1024; i++) {
            if ((unsigned char)rootDirEntries[i].name[0] == 0x00 || (unsigned char)rootDirEntries[i].name[0] == 0xE5) {
                numOfFirstFreeEntryInRootDir = i;
                break;
            }
        }
        if (numOfFirstFreeEntryInRootDir == -1) {
            printf("There is no free memory.\n");
            return -ENOSPC;
        }
        //выделяем кластер для файла
        int tmp = allocateNewCluster(-1, 0);
        if (tmp == -1) {
            printf("There is no free memory.\n");
            return -ENOSPC;
        } else if (tmp == -2) {
            printf("Error occurred while allocating memory for file\n");
            return -EIO;
        }
        unsigned short numOfClusterForFile = tmp;
        //заполняем информацию о файле
        strcpy(rootDirEntries[numOfFirstFreeEntryInRootDir].name, componentsOfPath[0]);
        rootDirEntries[numOfFirstFreeEntryInRootDir].numOfFirstCluster = numOfClusterForFile;
        rootDirEntries[numOfFirstFreeEntryInRootDir].permissions = mode;
        rootDirEntries[numOfFirstFreeEntryInRootDir].sizeInBytes = 0;
        rootDirEntries[numOfFirstFreeEntryInRootDir].modificationTime = time(NULL);
        rootDirEntries[numOfFirstFreeEntryInRootDir].lastAccessTime = time(NULL);
        rootDirEntries[numOfFirstFreeEntryInRootDir].creationTime = time(NULL);
        rootDirEntries[numOfFirstFreeEntryInRootDir].attributes = 0x00;
        //сохраняем номер кластера, с которого начинается файл
        fileInfo->fh = numOfClusterForFile;

        //записываем обратно измененные dirEntry корневого каталога
        if (rewriteRootDirEntries(rootDirEntries, 1024) == -1) {
            printf("Error occurred while creating file\n");
            return -EIO;
        }
    }
    if (currentDirEntries != rootDirEntries)
        free(currentDirEntries);
    free(rootDirEntries);
    fflush(state.container);
    return 0;
}

int sfsMkdir (const char* path, mode_t mode) {
    //копия пути к файлу
    char copyOfFullPath[255];
    strcpy(copyOfFullPath, path);
    //если не осталось свободных кластеров
    if (fsInfo.numOfFreeClusters == 0) {
        return -ENOSPC;
    }
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    //разбиение пути на компоненты
    if (splitPath(copyOfFullPath, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -ENAMETOOLONG;
    }
    DirEntry* rootDirEntries = readAllEntries();
    //если не удалось считать dirEntries корневого каталога
    if (rootDirEntries == NULL) {
        printf("Error occurred while reading dirEntries.\n");
        return -EIO;
    }
    /*проходим по компонентам пути, проверяя существуют ли каталоги и считывая их
    вложенные dirEntry относительно текущего каталога*/
    DirEntry latestDir;
    DirEntry* currentDirEntries = rootDirEntries; //dirEntry текущего каталога
    unsigned numOfSubDirs = 1024; //количество dirEntry текущего каталога
    unsigned short numOfParentDirCluster;
    for(int i = 0; i < depth; i++) {
        int index;
        //если каталог не существует
        if (!findDir(componentsOfPath[i], currentDirEntries, &index, numOfSubDirs)) {
            printf("Directory with name '%s' wasn't found.\n", componentsOfPath[i]);
            return -ENOENT;
        }
        //проверяем права на запись в текущий каталог
        if (!(currentDirEntries[index].permissions & (S_IWUSR | S_IWGRP | S_IWOTH))) {
            printf("Writing to the directory '%s' is prohibited.\n", currentDirEntries[index].name);
            return -EACCES;
        }
        int numOfClusters;
        //сохраняем структуру родительского каталога
        latestDir = currentDirEntries[index];
        unsigned char* buffer = readClusters(currentDirEntries[index].numOfFirstCluster, &numOfClusters);
        //количество dirEntry, которые помещаются в кластеры
        unsigned numOfDirEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        //приводим байты из считанных кластеров к массиву dirEntry
        currentDirEntries = convertToDirEntries(buffer, numOfDirEntriesPerCluster * numOfClusters, numOfClusters);
        numOfSubDirs = numOfDirEntriesPerCluster * numOfClusters;
        numOfParentDirCluster = latestDir.numOfFirstCluster;
    }
    DirEntry newDir;
    //если каталог создается не в корневом каталоге
    if (depth != 0) {
        //если запись в этот каталог запрещена
        if (!(latestDir.permissions & (S_IWUSR | S_IWGRP | S_IWOTH))) {
            printf("Writing to the directory '%s' is prohibited.\n", latestDir.name);
            return -EACCES;
        }
        //если такой каталог уже существует
        if (checkForExistenceOfFileOrDirectory(currentDirEntries, numOfSubDirs, componentsOfPath[depth], 0x10)) {
            printf("Directory with that name already exists.\n");
            return -EEXIST;
        }
        //ищем последний кластер, принадлежащий этому каталогу
        unsigned short numOfLastCluster = findLastCluster(latestDir.numOfFirstCluster);
        unsigned char* lastCluster = readCluster(numOfLastCluster);
        unsigned short emptySlots = checkClusterForEmptySlots(lastCluster);
        //если нет свободного места
        if (emptySlots == 0) {
            int tmp = allocateNewCluster(numOfLastCluster, 1);
            if (tmp == -1) {
                printf("There is no free memory.\n");
                return -ENOSPC;
            } else if (tmp == -2) {
                printf("Error occurred while allocating memory\n");
                return -EIO;
            }
            numOfLastCluster = tmp;
            lastCluster = readCluster(numOfLastCluster);
        }
        //выделяем кластер для вложенного каталога
        int tmp = allocateNewCluster(-1, 1);
        if (tmp == -1) {
            printf("There is no free memory.\n");
            return -ENOSPC;
        } else if (tmp == -2) {
            printf("Error occurred while allocating memory for directory\n");
            return -EIO;
        }
        unsigned short numOfClusterForSubDir = tmp;
        unsigned short numOfEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        DirEntry *lastClusterEntries = convertToDirEntries(lastCluster, numOfEntriesPerCluster, 1);
        for (int i = 0; i < numOfEntriesPerCluster; i++) {
            if ((unsigned char)lastClusterEntries[i].name[0] == 0x00 || (unsigned char)lastClusterEntries[i].name[0] == 0xE5) {
                //заполняем информацию о подкаталоге
                strcpy(lastClusterEntries[i].name, componentsOfPath[depth]);
                lastClusterEntries[i].numOfFirstCluster = numOfClusterForSubDir;
                lastClusterEntries[i].permissions = mode;
                lastClusterEntries[i].attributes = 0x10;
                lastClusterEntries[i].creationTime = time(NULL);
                lastClusterEntries[i].lastAccessTime = time(NULL);
                lastClusterEntries[i].modificationTime = time(NULL);
                lastClusterEntries[i].sizeInBytes = countClustersInChain(lastClusterEntries[i].numOfFirstCluster) * fsInfo.sizeOfClusterInBytes;
                newDir = lastClusterEntries[i];
                break;
            }
        }
        //записываем обратно dirEntry, считанные из последнего кластера текущего каталога, в этот кластер
        if (writeDirEntriesToCluster(numOfLastCluster, lastClusterEntries) == -1) {
            printf("Error occurred while creating new directory\n");
            return -EIO;
        }
    }
    //когда каталог создается в корневом каталоге
    else {
        numOfParentDirCluster = MAX_CLUSTERS;
        //если такой каталог уже существует
        if (checkForExistenceOfFileOrDirectory(rootDirEntries, 1024, componentsOfPath[depth], 0x10)) {
            printf("Directory with that name already exists.\n");
            return -EEXIST;
        }
        int numOfFirstFreeEntryInRootDir = -1;
        for (int i = 0; i < 1024; i++) {
            if ((unsigned char)rootDirEntries[i].name[0] == 0x00 || (unsigned char)rootDirEntries[i].name[0] == 0xE5) {
                numOfFirstFreeEntryInRootDir = i;
                break;
            }
        }
        if (numOfFirstFreeEntryInRootDir == -1) {
            printf("There is no free memory.\n");
            return -ENOSPC;
        }
        //выделяем кластер для каталога
        int tmp = allocateNewCluster(-1, 1);
        if (tmp == -1) {
            printf("There is no free memory.\n");
            return -ENOSPC;
        } else if (tmp == -2) {
            printf("Error occurred while allocating memory for directory\n");
            return -EIO;
        }
        unsigned short numOfClusterForSubDir = tmp;
        //заполняем информацию о каталоге
        strcpy(rootDirEntries[numOfFirstFreeEntryInRootDir].name, componentsOfPath[0]);
        rootDirEntries[numOfFirstFreeEntryInRootDir].numOfFirstCluster = numOfClusterForSubDir;
        rootDirEntries[numOfFirstFreeEntryInRootDir].permissions = mode;
        rootDirEntries[numOfFirstFreeEntryInRootDir].sizeInBytes = countClustersInChain(rootDirEntries[numOfFirstFreeEntryInRootDir].numOfFirstCluster) * fsInfo.sizeOfClusterInBytes;
        rootDirEntries[numOfFirstFreeEntryInRootDir].modificationTime = time(NULL);
        rootDirEntries[numOfFirstFreeEntryInRootDir].lastAccessTime = time(NULL);
        rootDirEntries[numOfFirstFreeEntryInRootDir].creationTime = time(NULL);
        rootDirEntries[numOfFirstFreeEntryInRootDir].attributes = 0x10;
        newDir = rootDirEntries[numOfFirstFreeEntryInRootDir];
        //записываем обратно измененные dirEntry корневого каталога
        if (rewriteRootDirEntries(rootDirEntries, 1024) == -1) {
            printf("Error occurred while creating directory\n");
            return -EIO;
        }
    }
    DirEntry* currentAndParentDirs = (DirEntry*)calloc(2, sizeof(DirEntry));
    //создаем каталог '.'
    strcpy(currentAndParentDirs[0].name, ".");
    currentAndParentDirs[0].numOfFirstCluster = newDir.numOfFirstCluster;
    currentAndParentDirs[0].permissions = mode;
    currentAndParentDirs[0].attributes = 0x10;
    currentAndParentDirs[0].creationTime = newDir.creationTime;
    currentAndParentDirs[0].lastAccessTime = newDir.lastAccessTime;
    currentAndParentDirs[0].modificationTime = newDir.modificationTime;
    currentAndParentDirs[0].sizeInBytes = newDir.sizeInBytes;
    //создаем каталог '..'
    strcpy(currentAndParentDirs[1].name, "..");
    currentAndParentDirs[1].numOfFirstCluster = numOfParentDirCluster;
    currentAndParentDirs[1].permissions = mode;
    currentAndParentDirs[1].attributes = 0x10;
    currentAndParentDirs[1].creationTime = newDir.creationTime;
    currentAndParentDirs[1].lastAccessTime = newDir.lastAccessTime;
    currentAndParentDirs[1].modificationTime = newDir.modificationTime;
    //если новый каталог был создан не в корневом каталоге
    if (depth != 0) {
        currentAndParentDirs[1].sizeInBytes = countDirSize(latestDir.numOfFirstCluster);
    }
    else {
        long long sizeInBytes = 0;
        for (int i = 0; i < 1024; i++) {
            if ((unsigned char)rootDirEntries[i].name[0] != 0x00 && (unsigned char)rootDirEntries[i].name[0] != 0xE5) {
                //если не каталог
                if (rootDirEntries[i].attributes != 0x10) {
                    sizeInBytes += rootDirEntries[i].sizeInBytes;
                }
                //если каталог
                else {
                    sizeInBytes += countDirSize(rootDirEntries[i].numOfFirstCluster);
                }
            }
        }
        currentAndParentDirs[1].sizeInBytes = sizeInBytes;
    }
    //записываем в созданный каталог '.' и '..'
    if (writeDirEntriesToEmptyCluster(newDir.numOfFirstCluster, currentAndParentDirs) == -1) {
        printf("Error occurred while creating new directory\n");
        return -EIO;
    }
    return 0;
}

int sfsGetattr(const char* path, struct stat* buf) {
    //копия пути к файлу
    char copyOfFullPath[255];
    strcpy(copyOfFullPath, path);
    memset(buf, 0 ,sizeof(struct stat));
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    //разбиение пути на компоненты
    if (splitPath(copyOfFullPath, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        free(componentsOfPath);
        return -ENAMETOOLONG;
    }
    DirEntry* rootDirEntries = readAllEntries();
    //если не удалось считать dirEntries корневого каталога
    if (rootDirEntries == NULL) {
        printf("Error occurred while reading root directory.\n");
        free(componentsOfPath);
        free(rootDirEntries);
        return -EIO;
    }
    //если запрашиваются атрибуты корневого каталога
    if (depth == 0 && strcmp(componentsOfPath[depth], "/") == 0) {
        int counter = 0;
        long long sizeInBytes = 0;
        for (int i = 0; i < 1024; i++) {
            if ((unsigned char)rootDirEntries[i].name[0] != 0x00 && (unsigned char)rootDirEntries[i].name[0] != 0xE5 && rootDirEntries[i].attributes == 0x10) {
                counter++;
            }
            if ((unsigned char)rootDirEntries[i].name[0] != 0x00 && (unsigned char)rootDirEntries[i].name[0] != 0xE5 && strcmp(rootDirEntries[i].name, ".") != 0 && strcmp(rootDirEntries[i].name, "..") != 0) {
                //если не каталог
                if (rootDirEntries[i].attributes != 0x10) {
                    sizeInBytes += rootDirEntries[i].sizeInBytes;
                }
                //если каталог
                else {
                    sizeInBytes += countDirSize(rootDirEntries[i].numOfFirstCluster);
                }
            }
        }
        buf->st_nlink = counter;
        buf->st_uid = getuid();
        buf->st_gid = getgid();
        buf->st_size = sizeInBytes;
        //каталог с правами доступа, записи и выполнения для всех
        buf->st_mode = S_IFDIR | 0777;
        buf->st_blocks = ceil(sizeInBytes / 512.0);
        //время последнего доступа
        buf->st_atim = convertTimeToTimespec(time(NULL));
        free(componentsOfPath);
        free(rootDirEntries);
        return 0;
    }
    //проходим по компонентам пути, проверяя существуют ли каталоги и считывая их
    //вложенные dirEntry относительно текущего каталога
    DirEntry latestDir;
    DirEntry* currentDirEntries = rootDirEntries; //dirEntry текущего каталога
    unsigned numOfSubDirs = 1024; //количество dirEntry текущего каталога
    for(int i = 0; i < depth; i++) {
        int index;
        //если каталог не существует
        if (!findDir(componentsOfPath[i], currentDirEntries, &index, numOfSubDirs)) {
            printf("Directory with name '%s' wasn't found.\n", componentsOfPath[i]);
            free(componentsOfPath);
            free(rootDirEntries);
            return -ENOENT;
        }
        //проверяем права на чтение из текущего каталога
        if (!(currentDirEntries[index].permissions & (S_IRUSR | S_IRGRP | S_IROTH))) {
            printf("Reading from the directory '%s' is prohibited.\n", currentDirEntries[index].name);
            free(componentsOfPath);
            free(rootDirEntries);
            return -EACCES;
        }
        int numOfClusters;
        //сохраняем структуру последнего в пути каталога
        latestDir = currentDirEntries[index];
        unsigned char* buffer = readClusters(currentDirEntries[index].numOfFirstCluster, &numOfClusters);
        //количество dirEntry, которые помещаются в кластер
        unsigned numOfDirEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        //приводим байты из считанных кластеров к массиву dirEntry
        currentDirEntries = convertToDirEntries(buffer, numOfDirEntriesPerCluster * numOfClusters, numOfClusters);
        numOfSubDirs = numOfDirEntriesPerCluster * numOfClusters;
        free(buffer);
    }
    for (int i = 0; i < numOfSubDirs; i++) {
        if (strcmp(currentDirEntries[i].name, componentsOfPath[depth]) == 0) {
            if (!(currentDirEntries[i].permissions & (S_IRUSR | S_IRGRP | S_IROTH))) {
                printf("Reading from the directory '%s' is prohibited.\n", currentDirEntries[i].name);
                return -EACCES;
            }
            //права доступа
            if (currentDirEntries[i].attributes == 0x10) {
                buf->st_mode = S_IFDIR | currentDirEntries[i].permissions;
            }
            else {
                buf->st_mode = S_IFREG | currentDirEntries[i].permissions;
            }
            //количество жестких ссылок
            //если каталог
            if (currentDirEntries[i].attributes == 0x10 && strncmp(currentDirEntries[i].name, "..", 2) != 0) {
                //если каталог, для которого необходимо получить атрибуты, находится в корневом каталоге
                //читаем dirEntry каталога, для которого нужно получить атрибуты
                int numOfClusters;
                unsigned char* buffer = readClusters(currentDirEntries[i].numOfFirstCluster, &numOfClusters);
                //количество dirEntry, которые помещаются в кластеры
                unsigned numOfDirEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
                //приводим байты из считанных кластеров к массиву dirEntry
                DirEntry* entries = convertToDirEntries(buffer, numOfDirEntriesPerCluster * numOfClusters, numOfClusters);
                //количество жестких ссылок = кол-во подкаталогов + 1 (из-за ссылки на самого себя - '.')
                buf->st_nlink = countSubDirs(entries, numOfDirEntriesPerCluster * numOfClusters) + 1;
            }
            else if (strncmp(currentDirEntries[i].name, "..", 2) == 0) {
                //если родительский каталог - корневой
                if(currentDirEntries[i].numOfFirstCluster == MAX_CLUSTERS) {
                    int counter = 0;
                    for (int j = 0; j < 1024; j++) {
                        if ((unsigned char)rootDirEntries[j].name[0] != 0x00 && (unsigned char)rootDirEntries[j].name[0] != 0xE5 && rootDirEntries[j].attributes == 0x10) {
                            counter++;
                        }
                    }
                    buf->st_nlink = counter;
                }
                else { //+
                    int counter = 0;
                    for (int j = 0; j < numOfSubDirs; j++) {
                        if (currentDirEntries[j].attributes == 0x10 && (unsigned char)currentDirEntries[j].name[0] != 0x00 && (unsigned char)currentDirEntries[j].name[0] != 0xE5) {
                            counter++;
                        }
                    }
                    buf->st_nlink = counter;
                }
            }
            //если обычный файл
            else {
                buf->st_nlink = 1;
            }
            buf->st_uid = getuid();
            buf->st_gid = getgid();
            //если не каталог
            if (currentDirEntries[i].attributes != 0x10) {
                buf->st_size = currentDirEntries[i].sizeInBytes;
                //количество блоков по 512 байт, занимаемых файлом
                buf->st_blocks = ceil(currentDirEntries[i].sizeInBytes / 512.0);
            }
            //если каталог, то размер = кол-во выделенных кластеров * размер кластера
            else {
                unsigned sizeOfDir = countDirSize(currentDirEntries[i].numOfFirstCluster);
                buf->st_size = sizeOfDir;
                //количество блоков по 512 байт, занимаемых файлом
                buf->st_blocks = ceil(sizeOfDir / 512.0);
            }
            //время последнего доступа
            buf->st_atim = convertTimeToTimespec(time(NULL));
            currentDirEntries[i].lastAccessTime = time(NULL);
            //время изменения содержимого и изменения метаданных
            buf->st_ctim = buf->st_mtim = convertTimeToTimespec(currentDirEntries[i].modificationTime);
            //записываем изменения для currentDirEntries[i]
            //если текущий каталог в корневом каталоге
            if (depth == 0) {
                if (rewriteRootDirEntries(currentDirEntries, 1024) == -1) {
                    printf("Error occurred while writing to container.\n");
                    free(componentsOfPath);
                    free(rootDirEntries);
                    return -EIO;
                }
            }
            else {
                if (rewriteDirEntries(latestDir.numOfFirstCluster, currentDirEntries, numOfSubDirs) == -1) {
                    printf("Error occurred while writing to container.\n");
                    free(componentsOfPath);
                    free(rootDirEntries);
                    return -EIO;
                }
            }
            free(componentsOfPath);
            free(rootDirEntries);
            return 0;
        }
    }
    printf("'%s': No such file or directory.\n", componentsOfPath[depth]);
    free(componentsOfPath);
    free(rootDirEntries);
    return -ENOENT;
}

int sfsOpen (const char* path, struct fuse_file_info* fi) {
    //копия пути к файлу
    char copyOfFullPath[255];
    strcpy(copyOfFullPath, path);
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    //разбиение пути на компоненты
    if (splitPath(copyOfFullPath, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -ENAMETOOLONG;
    }
    DirEntry* rootDirEntries = readAllEntries();
    //если не удалось считать dirEntries корневого каталога
    if (rootDirEntries == NULL) {
        printf("Error occurred while reading root directory.\n");
        return -EIO;
    }
    //проходим по компонентам пути, проверяя существуют ли каталоги и считывая их
    //вложенные dirEntry относительно текущего каталога
    DirEntry* currentDirEntries = rootDirEntries; //dirEntry текущего каталога
    DirEntry latestDir;
    unsigned numOfSubDirs = 1024; //количество dirEntry текущего каталога
    for(int i = 0; i < depth; i++) {
        int index;
        //если каталог не существует
        if (!findDir(componentsOfPath[i], currentDirEntries, &index, numOfSubDirs)) {
            printf("Directory with name '%s' wasn't found.\n", componentsOfPath[i]);
            return -ENOENT;
        }
        //проверяем права на чтение из текущего каталога
        if (!(currentDirEntries[index].permissions & (S_IRUSR | S_IRGRP | S_IROTH))) {
            printf("Reading from the directory '%s' is prohibited.\n", currentDirEntries[index].name);
            return -EACCES;
        }
        int numOfClusters;
        latestDir = currentDirEntries[index];
        unsigned char* buffer = readClusters(currentDirEntries[index].numOfFirstCluster, &numOfClusters);
        //количество dirEntry, которые помещаются в кластер
        unsigned numOfDirEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        //приводим байты из считанных кластеров к массиву dirEntry
        currentDirEntries = convertToDirEntries(buffer, numOfDirEntriesPerCluster * numOfClusters, numOfClusters);
        numOfSubDirs = numOfDirEntriesPerCluster * numOfClusters;
    }
    //проходим по всем dirEntry текущего каталога и ищем нужный нам файл
    for (int i = 0; i < numOfSubDirs; i++) {
        if (strncmp(currentDirEntries[i].name, componentsOfPath[depth], strlen(componentsOfPath[depth])) == 0) {
            fi->fh = currentDirEntries[i].numOfFirstCluster;
            currentDirEntries[i].lastAccessTime = time(NULL);
            //записываем изменения для currentDirEntries[i]
            //если текущий каталог в корневом каталоге
            if (depth == 0) {
                if (rewriteRootDirEntries(currentDirEntries, 1024) == -1) {
                    printf("Error occurred while writing to container.\n");
                    return -EIO;
                }
            }
            else {
                if (rewriteDirEntries(latestDir.numOfFirstCluster, currentDirEntries, numOfSubDirs) == -1) {
                    printf("Error occurred while writing to container.\n");
                    return -EIO;
                }
            }
            return 0;
        }
    }
    printf("'%s': No such file.\n", componentsOfPath[depth]);
    return -ENOENT;
}

int sfsRead (const char* path, char* buffer, size_t bytes, off_t offset, struct fuse_file_info* fi) {
    //копия пути к файлу
    char copyOfFullPath[255];
    strcpy(copyOfFullPath, path);
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    //разбиение пути на компоненты
    if (splitPath(copyOfFullPath, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -ENAMETOOLONG;
    }
    DirEntry* rootDirEntries = readAllEntries();
    //если не удалось считать dirEntries корневого каталога
    if (rootDirEntries == NULL) {
        printf("Error occurred while reading root directory.\n");
        return -EIO;
    }
    //проходим по компонентам пути, проверяя существуют ли каталоги и считывая их
    //вложенные dirEntry относительно текущего каталога
    DirEntry latestDir;
    DirEntry* currentDirEntries = rootDirEntries; //dirEntry текущего каталога
    unsigned numOfSubDirs = 1024; //количество dirEntry текущего каталога
    for(int i = 0; i < depth; i++) {
        int index;
        //если каталог не существует
        if (!findDir(componentsOfPath[i], currentDirEntries, &index, numOfSubDirs)) {
            printf("Directory with name '%s' wasn't found.\n", componentsOfPath[i]);
            return -ENOENT;
        }
        //проверяем права на чтение из текущего каталога
        if (!(currentDirEntries[index].permissions & (S_IRUSR | S_IRGRP | S_IROTH))) {
            printf("Reading from the directory '%s' is prohibited.\n", currentDirEntries[index].name);
            return -EACCES;
        }
        int numOfClusters;
        latestDir = currentDirEntries[index];
        unsigned char* buf = readClusters(currentDirEntries[index].numOfFirstCluster, &numOfClusters);
        //количество dirEntry, которые помещаются в кластер
        unsigned numOfDirEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        //приводим байты из считанных кластеров к массиву dirEntry
        currentDirEntries = convertToDirEntries(buf, numOfDirEntriesPerCluster * numOfClusters, numOfClusters);
        numOfSubDirs = numOfDirEntriesPerCluster * numOfClusters;
    }
    //ищем нужный нам файл
    for(int i = 0; i < numOfSubDirs; i++) {
        //если нашли нужный нам файл
        if (strcmp(currentDirEntries[i].name, componentsOfPath[depth]) == 0) {
            if (currentDirEntries[i].attributes != 0x00) {
                printf("Required entry is not a file\n");
                if (currentDirEntries[i].attributes == 0x10) {
                    return -EISDIR;
                }
                else {
                    return -EPERM;
                }
            }
            //проверяем права на чтение файла
            if (!(currentDirEntries[i].permissions & (S_IRUSR | S_IRGRP | S_IROTH))) {
                printf("Reading from file '%s' is prohibited.\n", componentsOfPath[depth]);
                return -EACCES;
            }
            //если смещение меньше, чем размер файла
            if (offset < currentDirEntries[i].sizeInBytes) {
                //если смещение + кол-во байт для чтения > размера файла
                if (offset + bytes > currentDirEntries[i].sizeInBytes) {
                    bytes = currentDirEntries[i].sizeInBytes - offset;
                }
                int numOfReadClusters;
                unsigned char* clustersData;
                unsigned char* tmp = readClusters(currentDirEntries[i].numOfFirstCluster, &numOfReadClusters);
                if (tmp == NULL) {
                    printf("Error occurred while reading from file\n");
                    return -EIO;
                }
                clustersData = tmp;
                memcpy(buffer, (char*)(clustersData + offset), bytes);
            }
            else {
                bytes = 0;
            }
            currentDirEntries[i].lastAccessTime = time(NULL);
            //записываем изменения для currentDirEntries[i]
            //если текущий каталог в корневом каталоге
            if (depth == 0) {
                if (rewriteRootDirEntries(currentDirEntries, 1024) == -1) {
                    printf("Error occurred while writing to container.\n");
                    return -EIO;
                }
            }
            else {
                if (rewriteDirEntries(latestDir.numOfFirstCluster, currentDirEntries, numOfSubDirs) == -1) {
                    printf("Error occurred while writing to container.\n");
                    return -EIO;
                }
            }
            break;
        }
    }
    return bytes;
}

int sfsWrite (const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    //копия пути к файлу
    char copyOfFullPath[255];
    strcpy(copyOfFullPath, path);
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    //разбиение пути на компоненты
    if (splitPath(copyOfFullPath, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -ENAMETOOLONG;
    }
    DirEntry* rootDirEntries = readAllEntries();
    //если не удалось считать dirEntries корневого каталога
    if (rootDirEntries == NULL) {
        printf("Error occurred while reading dirEntries.\n");
        return -EIO;
    }
    /*проходим по компонентам пути, проверяя существуют ли каталоги и считывая их
    вложенные dirEntry относительно текущего каталога*/
    DirEntry latestDir;
    DirEntry* currentDirEntries = rootDirEntries; //dirEntry текущего каталога
    unsigned numOfSubDirs = 1024; //количество dirEntry текущего каталога
    for(int i = 0; i < depth; i++) {
        int index;
        //если каталог не существует
        if (!findDir(componentsOfPath[i], currentDirEntries, &index, numOfSubDirs)) {
            printf("Directory with name '%s' wasn't found.\n", componentsOfPath[i]);
            return -ENOENT;
        }
        //проверяем права на запись в текущий каталог
        if (!(currentDirEntries[index].permissions & (S_IWUSR | S_IWGRP | S_IWOTH))) {
            printf("Writing to the directory '%s' is prohibited.\n", currentDirEntries[index].name);
            return -EACCES;
        }
        int numOfClusters;
        latestDir = currentDirEntries[index];
        unsigned char* buffer = readClusters(currentDirEntries[index].numOfFirstCluster, &numOfClusters);
        //количество dirEntry, которые помещаются в кластер
        unsigned numOfDirEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        //приводим байты из считанных кластеров к массиву dirEntry
        currentDirEntries = convertToDirEntries(buffer, numOfDirEntriesPerCluster * numOfClusters, numOfClusters);
        numOfSubDirs = numOfDirEntriesPerCluster * numOfClusters;
    }
    //ищем нужный нам файл
    for (int i = 0; i < numOfSubDirs; i++) {
        if (strcmp(currentDirEntries[i].name, componentsOfPath[depth]) == 0) {
            if (currentDirEntries[i].attributes != 0x00) {
                printf("Required entry is not a file\n");
                if (currentDirEntries[i].attributes == 0x10) {
                    return -EISDIR;
                }
                else {
                    return -EPERM;
                }
            }
            //проверяем права на запись в файл
            if (!(currentDirEntries[i].permissions & (S_IWUSR | S_IWGRP | S_IWOTH))) {
                printf("Writing to file '%s' is prohibited.\n", currentDirEntries[i].name);
                return -EACCES;
            }
            //если смещение+кол-во байт для записи больше, чем кол-во байт выделенных под этот файл
            if (offset + size > countClustersInChain(currentDirEntries[i].numOfFirstCluster) * fsInfo.sizeOfClusterInBytes) {
                //количество кластеров, которые необходимо дополнительно выделить
                unsigned short numOfClustersToAllocate = ceil((offset + size - countClustersInChain(currentDirEntries[i].numOfFirstCluster) * fsInfo.sizeOfClusterInBytes) / (double)fsInfo.sizeOfClusterInBytes);
                if (numOfClustersToAllocate > fsInfo.numOfFreeClusters) {
                    printf("Not enough memory.\n");
                    return -ENOSPC;
                }
                //номер текущего последнего кластера
                unsigned short numOfLastCluster = findLastCluster(currentDirEntries[i].numOfFirstCluster);
                //выделяем нужное количество кластеров
                for (int j = 0; j < numOfClustersToAllocate; j++) {
                    numOfLastCluster = allocateNewCluster(numOfLastCluster, 0);
                }
            }
            int numOfReadClusters;
            //читаем данные из файла
            unsigned char* fileClusters = readClusters(currentDirEntries[i].numOfFirstCluster, &numOfReadClusters);
            //копируем байты из переданного буфера
            memcpy(fileClusters + offset, (unsigned char*)buf, size);
            //записываем обновленные данные обратно в файл-контейнер
            if (writeBytesToFile(currentDirEntries[i].numOfFirstCluster, fileClusters) == -1) {
                printf("Error occurred while writing to file.\n");
                return -EIO;
            }
            if (offset + size > currentDirEntries[i].sizeInBytes) {
                currentDirEntries[i].sizeInBytes = offset + size;
            }
            currentDirEntries[i].modificationTime = time(NULL);
            currentDirEntries[i].lastAccessTime = time(NULL);
            //записываем изменения для currentDirEntries[i]
            //если это корневой каталог
            if (depth == 0) {
                if (rewriteRootDirEntries(currentDirEntries, 1024) == -1) {
                    printf("Error occurred while writing to container.\n");
                    return -EIO;
                }
            }
            else {
                if (rewriteDirEntries(latestDir.numOfFirstCluster, currentDirEntries, numOfSubDirs) == -1) {
                    printf("Error occurred while writing to container.\n");
                    return -EIO;
                }
            }
            return size;
        }
    }
    printf("'%s': no such file or directory\n", componentsOfPath[depth]);
    return -ENOENT;
}

int sfsReaddir (const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi) {
    //копия пути к файлу
    char copyOfFullPath[255];
    strcpy(copyOfFullPath, path);
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    //разбиение пути на компоненты
    if (splitPath(copyOfFullPath, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -ENAMETOOLONG;
    }
    DirEntry* rootDirEntries = readAllEntries();
    //если не удалось считать dirEntries корневого каталога
    if (rootDirEntries == NULL) {
        printf("Error occurred while reading root directory.\n");
        return -EIO;
    }
    //проходим по компонентам пути, проверяя существуют ли каталоги и считывая их
    //вложенные dirEntry относительно текущего каталога
    DirEntry* currentDirEntries = rootDirEntries; //dirEntry текущего каталога
    unsigned numOfSubDirs = 1024; //количество dirEntry текущего каталога
    for(int i = 0; i <= depth; i++) {
        int index;
        //если каталог не существует
        if (!findDir(componentsOfPath[i], currentDirEntries, &index, numOfSubDirs)) {
            if (strcmp(path, "/") != 0) {
                printf("Directory with name '%s' wasn't found.\n", componentsOfPath[i]);
                return -ENOENT;
            }
            //если текущий компонент пути - корневой каталог, то переходим к обработке следующего компонента
            if (strcmp(componentsOfPath[i], "/") == 0) {
                continue;
            }
        }
        //проверяем права на чтение из текущего каталога
        if (!(currentDirEntries[index].permissions & (S_IRUSR | S_IRGRP | S_IROTH))) {
            printf("Reading from the directory '%s' is prohibited.\n", currentDirEntries[index].name);
            return -EACCES;
        }
        int numOfClusters;
        unsigned char* buffer = readClusters(currentDirEntries[index].numOfFirstCluster, &numOfClusters);
        //количество dirEntry, которые помещаются в кластер
        unsigned numOfDirEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        //приводим байты из считанных кластеров к массиву dirEntry
        currentDirEntries = convertToDirEntries(buffer, numOfDirEntriesPerCluster * numOfClusters, numOfClusters);
        numOfSubDirs = numOfDirEntriesPerCluster * numOfClusters;
    }
    //вызываем функцию FUSE для записи информации о '.' и '..'
    filler(buf, ".", NULL, 0);
    if (strcmp(path, "/") != 0) {
        filler(buf, "..", NULL, 0);
    }
    for (int i = 0; i < numOfSubDirs; i++) {
        if ((unsigned char)currentDirEntries[i].name[0] != 0x00 && (unsigned char)currentDirEntries[i].name[0] != 0xE5) {
            //заполняем информацию о dirEntry
            struct stat statBuf;
            //права доступа
            //если каталог
            if (currentDirEntries[i].attributes == 0x10) {
                statBuf.st_mode = S_IFDIR | currentDirEntries[i].permissions;
            }
            //обычный файл
            else {
                statBuf.st_mode = S_IFREG | currentDirEntries[i].permissions;
            }
            //вызываем функцию FUSE, которая записывает информацию
            filler(buf, currentDirEntries[i].name, &statBuf, 0);
        }
    }
    return 0;
}

int sfsTruncate(const char* path, off_t size) {
    //копия пути к файлу
    char copyOfFullPath[255];
    strcpy(copyOfFullPath, path);
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    //разбиение пути на компоненты
    if (splitPath(copyOfFullPath, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -ENAMETOOLONG;
    }
    DirEntry* rootDirEntries = readAllEntries();
    //если не удалось считать dirEntries корневого каталога
    if (rootDirEntries == NULL) {
        printf("Error occurred while reading dirEntries.\n");
        return -EIO;
    }
    /*проходим по компонентам пути, проверяя существуют ли каталоги и считывая их
    вложенные dirEntry относительно текущего каталога*/
    DirEntry latestDir;
    DirEntry* currentDirEntries = rootDirEntries; //dirEntry текущего каталога
    unsigned numOfSubDirs = 1024; //количество dirEntry текущего каталога
    for(int i = 0; i < depth; i++) {
        int index;
        //если каталог не существует
        if (!findDir(componentsOfPath[i], currentDirEntries, &index, numOfSubDirs)) {
            printf("Directory with name '%s' wasn't found.\n", componentsOfPath[i]);
            return -ENOENT;
        }
        //проверяем права на запись в текущий каталог
        if (!(currentDirEntries[index].permissions & (S_IWUSR | S_IWGRP | S_IWOTH))) {
            printf("Writing to the directory '%s' is prohibited.\n", currentDirEntries[index].name);
            return -EACCES;
        }
        int numOfClusters;
        //сохраняем структуру последнего в пути каталога
        latestDir = currentDirEntries[index];
        unsigned char* buffer = readClusters(currentDirEntries[index].numOfFirstCluster, &numOfClusters);
        //количество dirEntry, которые помещаются в кластер
        unsigned numOfDirEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        //приводим байты из считанных кластеров к массиву dirEntry
        currentDirEntries = convertToDirEntries(buffer, numOfDirEntriesPerCluster * numOfClusters, numOfClusters);
        numOfSubDirs = numOfDirEntriesPerCluster * numOfClusters;
    }
    //ищем нужный нам файл
    for (int i = 0; i < numOfSubDirs; i++) {
        if (strcmp(currentDirEntries[i].name, componentsOfPath[depth]) == 0) {
            //проверяем права на запись в файл
            if (!(currentDirEntries[i].permissions & (S_IWUSR | S_IWGRP | S_IWOTH))) {
                printf("Writing to file '%s' is prohibited.\n", currentDirEntries[i].name);
                return -EACCES;
            }
            if (size == 0) {
                //количество кластеров, занятое файлом
                unsigned short numOfClustersTakenByFile = countClustersInChain(currentDirEntries[i].numOfFirstCluster);
                //пустые байты для записи в кластеры
                unsigned char* emptyClusters = (unsigned char*)calloc(numOfClustersTakenByFile * fsInfo.sizeOfClusterInBytes, sizeof(unsigned char));
                //записываем обнуленные данные обратно в файл
                if (writeBytesToFile(currentDirEntries[i].numOfFirstCluster, emptyClusters) == -1) {
                    printf("Error occurred while writing to file.\n");
                    return -EIO;
                }
                //освобождаем кластеры в цепочке, кроме первого
                freeClustersInChainExceptFirst(currentDirEntries[i].numOfFirstCluster);
            }
            //если файл нужно увеличить
            else if (currentDirEntries[i].sizeInBytes < size) {
                //последний кластер, выделенный файлу
                unsigned short numOfLastFileCluster = findLastCluster(currentDirEntries[i].numOfFirstCluster);
                unsigned char* lastFileCluster = readCluster(numOfLastFileCluster);
                if (lastFileCluster == NULL) {
                    printf("Error occurred while reading from file\n");
                    return -EIO;
                }
                //если разница в размере больше, чем число свободных байт в последнем кластере
                if (size - currentDirEntries[i].sizeInBytes > countFreeBytesInCluster(lastFileCluster)) {
                    //количество кластеров, которое нужно дополнительно выделить
                    unsigned short numOfClustersToAllocate = ceil((size - currentDirEntries[i].sizeInBytes - countFreeBytesInCluster(lastFileCluster)) / (double)fsInfo.sizeOfClusterInBytes);
                    if (numOfClustersToAllocate > fsInfo.numOfFreeClusters) {
                        printf("Not enough memory\n");
                        return -ENOMEM;
                    }
                    for (int j = 0; j < numOfClustersToAllocate; j++) {
                        int retVal = allocateNewCluster(numOfLastFileCluster, 0);
                        if (retVal == -1) {
                            printf("Not enough memory\n");
                            return -ENOMEM;
                        }
                        if (retVal == -2) {
                            printf("Error occurred while allocating memory for file\n");
                            return -EIO;
                        }
                    }
                }
            }
            //новый размер меньше старого
            else {
                unsigned short newNumOfClusters = ceil(size / (double)fsInfo.sizeOfClusterInBytes);
                //если необходимо освободить кластеры
                if (newNumOfClusters < countClustersInChain(currentDirEntries[i].numOfFirstCluster)) {
                    int oldNumOfClusters;
                    unsigned short* clustersInChain = getClustersFromChain(currentDirEntries[i].numOfFirstCluster, &oldNumOfClusters);
                    //освобождаем лишние кластеры
                    for (int j = oldNumOfClusters - 1; j > newNumOfClusters - 1; j--) {
                        if (freeCluster(clustersInChain[j]) == -1) {
                            printf("Error deallocating memory\n");
                            free(clustersInChain);
                            return -EIO;
                        }
                    }
                    //обозначаем кластер как последний в цепочке
                    FAT[clustersInChain[newNumOfClusters - 1]] = 0xFFFF;
                    free(clustersInChain);
                }
                //число байт, которое нужно обнулить в последнем кластере в цепочке
                unsigned short numOfBytesToZero = fsInfo.sizeOfClusterInBytes - (size % fsInfo.sizeOfClusterInBytes);
                if (zeroBytes(findLastCluster(currentDirEntries[i].numOfFirstCluster), numOfBytesToZero) == -1) {
                    printf("Error deallocating memory\n");
                    return -EIO;
                }
            }
            currentDirEntries[i].sizeInBytes = size;
            currentDirEntries[i].lastAccessTime = time(NULL);
            currentDirEntries[i].modificationTime = time(NULL);
            //сохраняем изменения
            if (depth == 0) {
                if (rewriteRootDirEntries(currentDirEntries, 1024) == -1) {
                    printf("Error occurred while setting access, modification time\n");
                    return -EIO;
                }
            }
            else {
                if (rewriteDirEntries(latestDir.numOfFirstCluster, currentDirEntries, numOfSubDirs) == -1) {
                    printf("Error occurred while setting access, modification time\n");
                    return -EIO;
                }
            }
            return 0;
        }
    }
    printf("'%s': no such file or directory\n", componentsOfPath[depth]);
    return -ENOENT;
}

int sfsUtimens (const char* path, const struct timespec tv[2]) {
    //копия пути к файлу
    char copyOfFullPath[255];
    strcpy(copyOfFullPath, path);
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    //разбиение пути на компоненты
    if (splitPath(copyOfFullPath, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -ENAMETOOLONG;
    }
    DirEntry* rootDirEntries = readAllEntries();
    //если не удалось считать dirEntries корневого каталога
    if (rootDirEntries == NULL) {
        printf("Error occurred while reading dirEntries.\n");
        return -EIO;
    }
    /*проходим по компонентам пути, проверяя существуют ли каталоги и считывая их
    вложенные dirEntry относительно текущего каталога*/
    DirEntry latestDir;
    DirEntry* currentDirEntries = rootDirEntries; //dirEntry текущего каталога
    unsigned numOfSubDirs = 1024; //количество dirEntry текущего каталога
    for(int i = 0; i < depth; i++) {
        int index;
        //если каталог не существует
        if (!findDir(componentsOfPath[i], currentDirEntries, &index, numOfSubDirs)) {
            printf("Directory with name '%s' wasn't found.\n", componentsOfPath[i]);
            return -ENOENT;
        }
        //проверяем права на запись в текущий каталог
        if (!(currentDirEntries[index].permissions & (S_IWUSR | S_IWGRP | S_IWOTH))) {
            printf("Writing to the directory '%s' is prohibited.\n", currentDirEntries[index].name);
            return -EACCES;
        }
        int numOfClusters;
        //сохраняем структуру последнего в пути каталога
        latestDir = currentDirEntries[index];
        unsigned char* buffer = readClusters(currentDirEntries[index].numOfFirstCluster, &numOfClusters);
        //количество dirEntry, которые помещаются в кластер
        unsigned numOfDirEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        //приводим байты из считанных кластеров к массиву dirEntry
        currentDirEntries = convertToDirEntries(buffer, numOfDirEntriesPerCluster * numOfClusters, numOfClusters);
        numOfSubDirs = numOfDirEntriesPerCluster * numOfClusters;
    }
    for (int i = 0; i < numOfSubDirs; i++) {
        //ищем нужный нам файл/каталог
        if (strcmp(currentDirEntries[i].name, componentsOfPath[depth]) == 0) {
            //проверяем права на запись в файл
            if (!(currentDirEntries[i].permissions & (S_IWUSR | S_IWGRP | S_IWOTH))) {
                printf("Writing in file/directory '%s' is prohibited.\n", currentDirEntries[i].name);
                return -EACCES;
            }
            //если нужно изменить время доступа
            if (tv[0].tv_nsec != UTIME_OMIT) {
                if (tv[0].tv_nsec == UTIME_NOW) {
                    currentDirEntries[i].lastAccessTime = time(NULL);
                }
                else {
                    currentDirEntries[i].lastAccessTime = convertTimeFromTimespec(tv[0]);
                }
            }
            //если нужно изменить время последнего изменения
            if (tv[1].tv_nsec != UTIME_OMIT) {
                if (tv[1].tv_nsec == UTIME_NOW) {
                    currentDirEntries[i].modificationTime = time(NULL);
                }
                else {
                    currentDirEntries[i].modificationTime = convertTimeFromTimespec(tv[1]);
                }
            }
            //если изменения записываются для каталога/файла, который находится в корневом каталоге
            if (depth == 0) {
                if (rewriteRootDirEntries(currentDirEntries, 1024) == -1) {
                    printf("Error occurred while setting access, modification time\n");
                    return -EIO;
                }
            }
            else {
                if (rewriteDirEntries(latestDir.numOfFirstCluster, currentDirEntries, numOfSubDirs) == -1) {
                    printf("Error occurred while setting access, modification time\n");
                    return -EIO;
                }
            }
            return 0;
        }
    }
    printf("'%s': no such file or directory\n", componentsOfPath[depth]);
    return -ENOENT;
}

int sfsRelease (const char* path, struct fuse_file_info* fi) {
    return 0;
}

int sfsUnlink (const char* path) {
    //копия пути к файлу
    char copyOfFullPath[255];
    strcpy(copyOfFullPath, path);
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    //разбиение пути на компоненты
    if (splitPath(copyOfFullPath, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -ENAMETOOLONG;
    }
    DirEntry* rootDirEntries = readAllEntries();
    //если не удалось считать dirEntries корневого каталога
    if (rootDirEntries == NULL) {
        printf("Error occurred while reading dirEntries.\n");
        return -EIO;
    }
    /*проходим по компонентам пути, проверяя существуют ли каталоги и считывая их
    вложенные dirEntry относительно текущего каталога*/
    DirEntry latestDir;
    DirEntry* currentDirEntries = rootDirEntries; //dirEntry текущего каталога
    unsigned numOfSubDirs = 1024; //количество dirEntry текущего каталога
    for(int i = 0; i < depth; i++) {
        int index;
        //если каталог не существует
        if (!findDir(componentsOfPath[i], currentDirEntries, &index, numOfSubDirs)) {
            printf("Directory with name '%s' wasn't found.\n", componentsOfPath[i]);
            return -ENOENT;
        }
        //проверяем права на запись в текущий каталог
        if (!(currentDirEntries[index].permissions & (S_IWUSR | S_IWGRP | S_IWOTH))) {
            printf("Writing to the directory '%s' is prohibited.\n", currentDirEntries[index].name);
            return -EACCES;
        }
        int numOfClusters;
        //сохраняем структуру последнего в пути каталога
        latestDir = currentDirEntries[index];
        unsigned char* buffer = readClusters(currentDirEntries[index].numOfFirstCluster, &numOfClusters);
        //количество dirEntry, которые помещаются в кластер
        unsigned numOfDirEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        //приводим байты из считанных кластеров к массиву dirEntry
        currentDirEntries = convertToDirEntries(buffer, numOfDirEntriesPerCluster * numOfClusters, numOfClusters);
        numOfSubDirs = numOfDirEntriesPerCluster * numOfClusters;
    }
    //ищем нужный файл по названию
    for (int i = 0; i < numOfSubDirs; i++) {
        if (strcmp(currentDirEntries[i].name, componentsOfPath[depth]) == 0) {
            //если не обычный файл
            if (currentDirEntries[i].attributes != 0x00) {
                printf("Required entry is not a regular file\n");
                if (currentDirEntries[i].attributes == 0x10) {
                    return -EISDIR;
                }
                else {
                    return -EPERM;
                }
            }
            //освобождаем кластеры в цепочке найденного файла
            freeAllClustersInChain(currentDirEntries[i].numOfFirstCluster);
            //обнуляем поля файла и устанавливаем первый байт имени
            currentDirEntries[i].name[0] = (char)0xE5;
            currentDirEntries[i].numOfFirstCluster = 0xFFFF;
            currentDirEntries[i].permissions = 0;
            currentDirEntries[i].sizeInBytes = 0;
            currentDirEntries[i].attributes = 0x00;
            currentDirEntries[i].modificationTime = time(NULL);
            currentDirEntries[i].lastAccessTime = time(NULL);
            currentDirEntries[i].creationTime = 0;
            //перезаписываем dirEntry родительского каталога

            //если удаляемый файл находится не в корневом каталоге
            if (depth != 0) {
                if (rewriteDirEntries(latestDir.numOfFirstCluster, currentDirEntries, numOfSubDirs) == -1) {
                    printf("Error occurred while writing to container\n");
                    return -EIO;
                }
                //если все dirEntry в кластере, где находился удаляемый файл, пустые, то освобождаем кластер
                if (freeClusterIfNeeded(i, latestDir.numOfFirstCluster) == -1) {
                    printf("Error occurred while reading from container\n");
                    return -EIO;
                }
                printf("after maybe freeing cluster of dir\n");
            }
            else {
                if (rewriteRootDirEntries(currentDirEntries, 1024) == -1) {
                    printf("Error occurred while writing to container\n");
                    return -EIO;
                }
            }
            if (depth != 0) {
                //изменяем время последнего изменения для родительского каталога
                char pathToLatestDir[255];
                strncpy(pathToLatestDir, path, strlen(path) - strlen(componentsOfPath[depth]) - 1);
                pathToLatestDir[strlen(path) - strlen(componentsOfPath[depth]) - 1] = '\0';
                struct timespec tv[2];
                tv[0].tv_nsec = UTIME_OMIT;
                tv[1] = convertTimeToTimespec(time(NULL));
                int retVal = sfsUtimens(pathToLatestDir, tv);
                if (retVal != 0) {
                    return retVal;
                }
            }
            return 0;
        }
    }
    printf("'%s': no such file\n", componentsOfPath[depth]);
    return -ENOENT;
}

int sfsRmdir (const char* path) {
    //копия пути к файлу
    char copyOfFullPath[255];
    strcpy(copyOfFullPath, path);
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    //разбиение пути на компоненты
    if (splitPath(copyOfFullPath, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -ENAMETOOLONG;
    }
    DirEntry* rootDirEntries = readAllEntries();
    //если не удалось считать dirEntries корневого каталога
    if (rootDirEntries == NULL) {
        printf("Error occurred while reading root directory.\n");
        return -EIO;
    }
    /*проходим по компонентам пути, проверяя существуют ли каталоги и считывая их
    вложенные dirEntry относительно текущего каталога*/
    DirEntry latestDir;
    DirEntry* currentDirEntries = rootDirEntries; //dirEntry текущего каталога
    unsigned numOfSubDirs = 1024; //количество dirEntry текущего каталога
    for(int i = 0; i < depth; i++) {
        int index;
        //если каталог не существует
        if (!findDir(componentsOfPath[i], currentDirEntries, &index, numOfSubDirs)) {
            printf("Directory with name '%s' wasn't found.\n", componentsOfPath[i]);
            return -ENOENT;
        }
        //проверяем права на запись в текущий каталог
        if (!(currentDirEntries[index].permissions & (S_IWUSR | S_IWGRP | S_IWOTH))) {
            printf("Writing to the directory '%s' is prohibited.\n", currentDirEntries[index].name);
            return -EACCES;
        }
        int numOfClusters;
        //сохраняем структуру последнего в пути каталога
        latestDir = currentDirEntries[index];
        unsigned char* buffer = readClusters(currentDirEntries[index].numOfFirstCluster, &numOfClusters);
        //количество dirEntry, которые помещаются в кластер
        unsigned numOfDirEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
        //приводим байты из считанных кластеров к массиву dirEntry
        currentDirEntries = convertToDirEntries(buffer, numOfDirEntriesPerCluster * numOfClusters, numOfClusters);
        numOfSubDirs = numOfDirEntriesPerCluster * numOfClusters;
    }
    //ищем нужный подкаталог по названию
    for (int i = 0; i < numOfSubDirs; i++) {
        if (strcmp(currentDirEntries[i].name, componentsOfPath[depth]) == 0) {
            //если не каталог
            if (currentDirEntries[i].attributes != 0x10) {
                printf("Required entry is not directory\n");
                return -ENOTDIR;
            }
            //если удаляется '.' или '..'
            if (strcmp(currentDirEntries[i].name, ".") == 0 || strcmp(currentDirEntries[i].name, "..") == 0) {
                printf("Cannot remove '.' or '..'\n");
                return -EINVAL;
            }
            //проверяем пустой ли каталог
            int numOfClusters;
            unsigned char* clustersOfDir = readClusters(currentDirEntries[i].numOfFirstCluster, &numOfClusters);
            if (clustersOfDir == NULL) {
                printf("Error occurred while reading from container\n");
                return -EIO;
            }
            unsigned short numOfEntriesPerCluster = fsInfo.sizeOfClusterInBytes / sizeof(DirEntry);
            DirEntry* entriesOfDir = convertToDirEntries(clustersOfDir, numOfClusters * numOfEntriesPerCluster, numOfClusters);
            for (int j = 0; j < numOfClusters * numOfEntriesPerCluster; j++) {
                if (entriesOfDir[j].name[0] != 0x00 && (unsigned char)entriesOfDir[j].name[0] != 0xE5 && strcmp(entriesOfDir[j].name, ".") != 0 && strcmp(entriesOfDir[j].name, "..") != 0) {
                    printf("Cannot remove not empty directory\n");
                    return -ENOTEMPTY;
                }
            }
            //освобождаем кластеры в цепочке найденного каталога
            freeAllClustersInChain(currentDirEntries[i].numOfFirstCluster);
            //обнуляем поля каталога и устанавливаем первый байт имени
            currentDirEntries[i].name[0] = (char)0xE5;
            currentDirEntries[i].numOfFirstCluster = 0xFFFF;
            currentDirEntries[i].permissions = 0;
            currentDirEntries[i].sizeInBytes = 0;
            currentDirEntries[i].attributes = 0x00;
            currentDirEntries[i].modificationTime = time(NULL);
            currentDirEntries[i].lastAccessTime = time(NULL);
            currentDirEntries[i].creationTime = 0;
            //перезаписываем dirEntry родительского каталога

            //если удаляемый каталог находится не в корневом каталоге
            if (depth != 0) {
                if (rewriteDirEntries(latestDir.numOfFirstCluster, currentDirEntries, numOfSubDirs) == -1) {
                    printf("Error occurred while writing to container\n");
                    return -EIO;
                }
                //если все dirEntry в кластере, где находился удаляемый каталог, пустые, то освобождаем кластер
                if (freeClusterIfNeeded(i, latestDir.numOfFirstCluster) == -1) {
                    printf("Error occurred while reading from container\n");
                    return -EIO;
                }
            }
            else {
                if (rewriteRootDirEntries(currentDirEntries, 1024) == -1) {
                    printf("Error occurred while writing to container\n");
                    return -EIO;
                }
            }
            if (depth != 0) {
                //изменяем время последнего изменения для родительского каталога
                char pathToLatestDir[255];
                strncpy(pathToLatestDir, path, strlen(path) - strlen(componentsOfPath[depth]) - 1);
                pathToLatestDir[strlen(path) - strlen(componentsOfPath[depth]) - 1] = '\0';
                struct timespec tv[2];
                tv[0].tv_nsec = UTIME_OMIT;
                tv[1] = convertTimeToTimespec(time(NULL));
                int retVal = sfsUtimens(pathToLatestDir, tv);
                if (retVal != 0) {
                    return retVal;
                }
            }
            return 0;
        }
    }
    printf("'%s': no such directory\n", componentsOfPath[depth]);
    return -ENOENT;
}
