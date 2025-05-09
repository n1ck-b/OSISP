#include "fuse_functions.h"

#define SIZE_OF_CONTAINER 269484032 //размер (257 МБ) "контейнера" в байтах, где будет размещаться файловая система

extern FSInfo fsInfo;
extern unsigned short FAT[MAX_CLUSTERS];
extern FsState state;

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
            printf("7 Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
        if (fread(&fsInfo, sizeof(FSInfo), 1, container) < 1) {
            printf("1 Error occurred while initializing file system\n");
            exit(EXIT_FAILURE);
        }
        if (fseek(container, fsInfo.sizeOfClusterInBytes, SEEK_SET) != 0) {
            printf("2 Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
        if (fread(&FAT, sizeof(unsigned short), MAX_CLUSTERS, container) < MAX_CLUSTERS) {
            printf("3 Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
    }
    else {
        //если "контейнер" не существует
        container = fopen(pathToFile, "w+b");
        if (container == NULL) {
            printf("4 Error occurred while creating sfs.img file.\n");
            exit(EXIT_FAILURE);
        }
        if (fwrite(&fsInfo, sizeof(FSInfo), 1, container) < 1) {
            printf("5 Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
        char emptyByte = 0;  //нулевой байт
        fwrite(&emptyByte, sizeof(char), fsInfo.sizeOfClusterInBytes - sizeof(FSInfo), container);
        if (fwrite(&FAT, sizeof(unsigned short), MAX_CLUSTERS, container) < MAX_CLUSTERS) {
            printf("6 Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
        DirEntry emptyEntry;
        strcpy(emptyEntry.name, "0x00");
        //запись пустых DirEntry
        if (fwrite(&emptyEntry, sizeof(DirEntry), /*1023*/ 1024, container) < /*1023*/ 1024) {
            printf("7 Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
        //заполняем область данных пустыми байтами
        if (fwrite(&emptyByte, sizeof(char), MAX_CLUSTERS * fsInfo.sizeOfClusterInBytes, container) < MAX_CLUSTERS * fsInfo.sizeOfClusterInBytes) {
            printf("8 Error occurred while initializing file system.\n");
            exit(EXIT_FAILURE);
        }
    }
    strcpy(state.pathToContainer, pathToFile);
    state.container = container;
    free(pathToFile);
    printf("\nEnd of init\n");
    return NULL;
}

void sfsDestroy(void *private_data) {
    fclose(state.container);
}

int sfsCreate (const char* path, mode_t mode, struct fuse_file_info* fileInfo) {
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
    if (splitPath(path, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -EINVAL;
    }
    //проходим по компонентам пути, проверяя существуют ли каталоги и считывая их
    //вложенные dirEntry относительно текущего каталога
    DirEntry latestDir;
    DirEntry* currentDirEntries = rootDirEntries;
    unsigned numOfSubDirs = 1024;
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
    //если файл создается не в корневом каталоге
    if (depth != 0) {
        //если запись в этот каталог запрещена
        if (!(latestDir.permissions & (S_IWUSR | S_IWGRP | S_IWOTH))) {
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
        unsigned char *lastCluster = readCluster(numOfLastCluster);
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
        DirEntry *lastClusterEntries = convertToDirEntries(lastCluster, numOfEntriesPerCluster, 1);
        for (int i = 0; i < numOfEntriesPerCluster; i++) {
            if (strcmp(lastClusterEntries[i].name, "0x00") == 0 || strcmp(lastClusterEntries[i].name, "0xE5") == 0) {
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
            if (strcmp(rootDirEntries[i].name, "0x00") == 0 || strcmp(rootDirEntries[i].name, "0xE5") == 0) {
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
    return 0;
}

int sfsMkdir (const char* path, mode_t mode) {
    //если не осталось свободных кластеров
    if (fsInfo.numOfFreeClusters == 0) {
        return -ENOSPC;
    }
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    if (splitPath(path, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -EINVAL;
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
    DirEntry* currentDirEntries = rootDirEntries;
    unsigned numOfSubDirs = 1024;
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
            if (strcmp(lastClusterEntries[i].name, "0x00") == 0 || strcmp(lastClusterEntries[i].name, "0xE5") == 0) {
                //заполняем информацию о подкаталоге
                strcpy(lastClusterEntries[i].name, componentsOfPath[depth]);
                lastClusterEntries[i].numOfFirstCluster = numOfClusterForSubDir;
                lastClusterEntries[i].permissions = mode;
                lastClusterEntries[i].attributes = 0x10;
                lastClusterEntries[i].creationTime = time(NULL);
                lastClusterEntries[i].lastAccessTime = time(NULL);
                lastClusterEntries[i].modificationTime = time(NULL);
                lastClusterEntries[i].sizeInBytes = 0;
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
            if (strcmp(rootDirEntries[i].name, "0x00") == 0 || strcmp(rootDirEntries[i].name, "0xE5") == 0) {
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
        rootDirEntries[numOfFirstFreeEntryInRootDir].sizeInBytes = 0;
        rootDirEntries[numOfFirstFreeEntryInRootDir].modificationTime = time(NULL);
        rootDirEntries[numOfFirstFreeEntryInRootDir].lastAccessTime = time(NULL);
        rootDirEntries[numOfFirstFreeEntryInRootDir].creationTime = time(NULL);
        rootDirEntries[numOfFirstFreeEntryInRootDir].attributes = 0x10;
        newDir = rootDirEntries[numOfFirstFreeEntryInRootDir];
        //записываем обратно измененные dirEntry корневого каталога
        if(fseek(state.container, fsInfo.sizeOfClusterInBytes + fsInfo.sizeOfFatInBytes, SEEK_SET) != 0) {
            printf("Error occurred while creating directory\n");
            return -EIO;
        }
        if (fwrite(rootDirEntries, sizeof(DirEntry), 1024, state.container) < 1024) {
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
    currentAndParentDirs[1].sizeInBytes = newDir.sizeInBytes;
    //записываем в созданный каталог '.' и '..'
    if (writeDirEntriesToEmptyCluster(newDir.numOfFirstCluster, currentAndParentDirs) == -1) {
        printf("Error occurred while creating new directory\n");
        return -EIO;
    }
    return 0;
}

int sfsGetattr(const char* path, struct stat* buf) {
    if (state.container == NULL) {
        printf("Container is not open\n");
    }
    memset(buf, 0 ,sizeof(struct stat));
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    if (splitPath(path, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        free(componentsOfPath);
        return -EINVAL;
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
            if (strcmp(rootDirEntries[i].name, "0x00") != 0 && strcmp(rootDirEntries[i].name, "0xE5") != 0 && rootDirEntries[i].attributes == 0x10) {
                counter++;
            }
            if (strcmp(rootDirEntries[i].name, "0x00") != 0 && strcmp(rootDirEntries[i].name, "0xE5") != 0) {
                sizeInBytes += rootDirEntries[i].sizeInBytes;
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
        buf->st_atim = convertTime(time(NULL));
        free(componentsOfPath);
        free(rootDirEntries);
        return 0;
    }
    //проходим по компонентам пути, проверяя существуют ли каталоги и считывая их
    //вложенные dirEntry относительно текущего каталога
    DirEntry latestDir;
    DirEntry* currentDirEntries = rootDirEntries;
    unsigned numOfSubDirs = 1024;
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
            //права доступа
            buf->st_mode = currentDirEntries[i].permissions;
            //количество жестких ссылок
            //если каталог
            if (currentDirEntries[i].attributes == 0x10 && strcmp(currentDirEntries[i].name, "..") != 0) {
                //если каталог, для которого необходимо получить атрибуты, находится в корневом каталоге
                if (depth == 0) {
                    //читаем dirEntry каталога, для которого нужно получить атрибуты
                    int numOfClusters;
                    unsigned char* buffer = readClusters(currentDirEntries[i].numOfFirstCluster, &numOfClusters);
                    //количество dirEntry, которые помещаются в кластеры
                    unsigned numOfDirEntriesPerClusters = (fsInfo.sizeOfClusterInBytes * numOfClusters) / sizeof(DirEntry);
                    //приводим байты из считанных кластеров к массиву dirEntry
                    DirEntry* entries = convertToDirEntries(buffer, numOfDirEntriesPerClusters * numOfClusters, numOfClusters);
                    //количество жестких ссылок = кол-во подкаталогов + 1 (из-за ссылки на самого себя - '.')
                    buf->st_nlink = countSubDirs(entries, numOfDirEntriesPerClusters * numOfClusters) + 1;
                }
                else {
                    //количество жестких ссылок = кол-во подкаталогов + 1 (из-за ссылки на самого себя - '.')
                    buf->st_nlink = countSubDirs(currentDirEntries, numOfSubDirs) + 1;
                }
            }
            else if (strcmp(currentDirEntries[i].name, "..") == 0) {
                //если родительский каталог - корневой
                if(currentDirEntries[i].numOfFirstCluster == MAX_CLUSTERS) {
                    int counter = 0;
                    for (int j = 0; j < 1024; j++) {
                        if (strcmp(rootDirEntries[j].name, "0x00") != 0 && strcmp(rootDirEntries[j].name, "0xE5") != 0 && rootDirEntries[j].attributes == 0x10) {
                            counter++;
                        }
                    }
                    buf->st_nlink = counter;
                }
                else {
                    int counter = 0;
                    for (int j = 0; j < numOfSubDirs; j++) {
                        if (currentDirEntries[j].attributes == 0x10 && strcmp(currentDirEntries[j].name, "0x00") != 0 && strcmp(currentDirEntries[j].name, "0xE5") != 0) {
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
            buf->st_size = currentDirEntries[i].sizeInBytes;
            //количество блоков по 512 байт, занимаемых файлом
            buf->st_blocks = ceil(currentDirEntries[i].sizeInBytes / 512.0);
            //время последнего доступа
            buf->st_atim = convertTime(time(NULL));
            currentDirEntries[i].lastAccessTime = time(NULL);
            //время изменения содержимого и изменения метаданных
            buf->st_ctim = buf->st_mtim = convertTime(currentDirEntries[i].modificationTime);
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
    printf("\n2 free\n");
    free(componentsOfPath);
    printf("\n3 free\n");
    free(rootDirEntries);
    return -ENOENT;
}

int sfsOpen (const char* path, struct fuse_file_info* fi) {
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    if (splitPath(path, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -EINVAL;
    }
    DirEntry* rootDirEntries = readAllEntries();
    //если не удалось считать dirEntries корневого каталога
    if (rootDirEntries == NULL) {
        printf("Error occurred while reading root directory.\n");
        return -EIO;
    }
    //проходим по компонентам пути, проверяя существуют ли каталоги и считывая их
    //вложенные dirEntry относительно текущего каталога
    DirEntry* currentDirEntries = rootDirEntries;
    DirEntry latestDir;
    unsigned numOfSubDirs = 1024;
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
        if (strcmp(currentDirEntries[i].name, componentsOfPath[depth]) == 0) {
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
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    if (splitPath(path, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -EINVAL;
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
    DirEntry* currentDirEntries = rootDirEntries;
    unsigned numOfSubDirs = 1024;
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
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    if (splitPath(path, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -EINVAL;
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
    DirEntry* currentDirEntries = rootDirEntries;
    unsigned numOfSubDirs = 1024;
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
            //записываем обновленные данные обратно в файл
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
            break;
        }
    }
    return 0;
}

int sfsReaddir (const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi) {
    char** componentsOfPath = (char**)calloc(MAX_DEPTH + 1, sizeof(char*));
    for(int i = 0; i < MAX_DEPTH + 1; i++) {
        componentsOfPath[i] = (char*)calloc(255, sizeof(char));
    }
    int depth;
    if (splitPath(path, &componentsOfPath, &depth) == -1) {
        printf("Invalid name of file/directory. Name must be no longer than 255 characters.\n");
        return -EINVAL;
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
    DirEntry* currentDirEntries = rootDirEntries;
    unsigned numOfSubDirs = 1024;
    for(int i = 0; i <= depth; i++) {
        int index;
        //если каталог не существует
        if (!findDir(componentsOfPath[i], currentDirEntries, &index, numOfSubDirs)) {
            if (strcmp(path, "/") != 0) {
                printf("Directory with name '%s' wasn't found.\n", componentsOfPath[i]);
                return -ENOENT;
            }
        }
        //проверяем права на чтение из текущего каталога
        if (!(currentDirEntries[index].permissions & (S_IRUSR | S_IRGRP | S_IROTH))) {
            printf("Reading from the directory '%s' is prohibited.\n", currentDirEntries[index].name);
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
        if (strcmp(currentDirEntries[i].name, "0x00") != 0 && strcmp(currentDirEntries[i].name, "0xE5") != 0) {
            //заполняем информацию о dirEntry
            struct stat statBuf;
            //права доступа
            statBuf.st_mode = currentDirEntries[i].permissions;
            //количество жестких ссылок
            //если каталог
            if (currentDirEntries[i].attributes == 0x10 && strcmp(currentDirEntries[i].name, "..") != 0) {
                //если каталог, для которого необходимо получить атрибуты, находится в корневом каталоге
                if (depth == 0) {
                    //читаем dirEntry каталога, для которого нужно получить атрибуты
                    int numOfClusters;
                    unsigned char* buffer = readClusters(currentDirEntries[i].numOfFirstCluster, &numOfClusters);
                    //количество dirEntry, которые помещаются в кластеры
                    unsigned numOfDirEntriesPerClusters = (fsInfo.sizeOfClusterInBytes * numOfClusters) / sizeof(DirEntry);
                    //приводим байты из считанных кластеров к массиву dirEntry
                    DirEntry* entries = convertToDirEntries(buffer, numOfDirEntriesPerClusters * numOfClusters, numOfClusters);
                    //количество жестких ссылок = кол-во подкаталогов + 1 (из-за ссылки на самого себя - '.')
                    statBuf.st_nlink = countSubDirs(entries, numOfDirEntriesPerClusters * numOfClusters) + 1;
                }
                else {
                    //количество жестких ссылок = кол-во подкаталогов + 1 (из-за ссылки на самого себя - '.')
                    statBuf.st_nlink = countSubDirs(currentDirEntries, numOfSubDirs) + 1;
                }
            }
            else if (strcmp(currentDirEntries[i].name, "..") == 0) {
                //если родительский каталог - корневой
                if(currentDirEntries[i].numOfFirstCluster == MAX_CLUSTERS) {
                    int counter = 0;
                    //считаем каталоги, находящиеся в корневом каталоге
                    for (int j = 0; j < 1024; j++) {
                        if (strcmp(rootDirEntries[j].name, "0x00") != 0 && strcmp(rootDirEntries[j].name, "0xE5") != 0 && rootDirEntries[j].attributes == 0x10) {
                            counter++;
                        }
                    }
                    statBuf.st_nlink = counter;
                }
                else {
                    //считаем каталоги, находящиеся в родительском (относительно текущего) каталоге
                    int numOfClusters;
                    unsigned char* buffer = readClusters(latestDir.numOfFirstCluster, &numOfClusters);
                    //количество dirEntry, которые помещаются в кластеры
                    unsigned numOfDirEntriesPerClusters = (fsInfo.sizeOfClusterInBytes * numOfClusters) / sizeof(DirEntry);
                    //приводим байты из считанных кластеров к массиву dirEntry
                    DirEntry* entries = convertToDirEntries(buffer, numOfDirEntriesPerClusters * numOfClusters, numOfClusters);
                    //количество жестких ссылок = кол-во подкаталогов + 1 (из-за ссылки на самого себя - '.')
                    statBuf.st_nlink = countSubDirs(entries, numOfDirEntriesPerClusters * numOfClusters) + 1;
                }
            }
            //если обычный файл
            else {
                statBuf.st_nlink = 1;
            }
            statBuf.st_uid = getuid();
            statBuf.st_gid = getgid();
            statBuf.st_size = currentDirEntries[i].sizeInBytes;
            //количество блоков по 512 байт, занимаемых файлом
            statBuf.st_blocks = ceil(currentDirEntries[i].sizeInBytes / 512.0);
            //время последнего доступа
            statBuf.st_atim = convertTime(time(NULL));
            //время изменения содержимого и изменения метаданных
            statBuf.st_ctim = statBuf.st_mtim = convertTime(currentDirEntries[i].modificationTime);

            //вызываем функцию FUSE, которая записывает информацию
            filler(buf, currentDirEntries[i].name, &statBuf, 0);
        }
    }
    return 0;
}