#include "sort_index.h"
#include "gen.h"
#include "threads.h"

IndexHeader indexHeader;
pthread_t *threads;
void* buffer;
pthread_barrier_t barrier;
int* blockMap;
pthread_mutex_t* mutex;
int numOfBlocks;
volatile int bufferIsReady;

extern int getpagesize();

void generateFile(char* fileName) {
    char** args = (char**)calloc(3, sizeof(char*));
    for (int i = 0; i < 3; i++)
    {
        args[i] = (char*)calloc(100, sizeof(char));
    }
    snprintf(args[0], 100, "%lu", indexHeader.records);
    strcpy(args[1], fileName);
    args[2] = (char*)0;
    pid_t pid = fork();
    if (pid == -1) {
        printf("Error starting 'gen' program: %s\n", strerror(errno));
        exit(errno);
    }
    if (pid == 0) {
        if (execve("./gen", args, NULL) == -1) {
            printf("Error starting 'gen' program: %s\n", strerror(errno));
            exit(errno);
        }
    }
    int status;
    wait(&status);
    if (status != EXIT_FAILURE) {
        printf("\nFile generated successfully\n");
    }
    else {
        exit(EXIT_FAILURE);
    }
    for(int i = 0; i < 3; i++) {
        free(args[i]);
    }
    free(args);
}

void displayFile(char* fileName) {
    char** args = (char**)calloc(3, sizeof(char*));
    for (int i = 0; i < 3; i++)
    {
        args[i] = (char*)calloc(100, sizeof(char));
    }
    snprintf(args[0], 100, "%lu", indexHeader.records);
    strcpy(args[1], fileName);
    args[2] = (char*)0;
    pid_t pid = fork();
    if (pid == -1) {
        printf("Error starting 'view' program: %s\n", strerror(errno));
        exit(errno);
    }
    if (pid == 0) {
        if (execve("./view", args, NULL) == -1) {
            printf("Error starting 'view' program: %s\n", strerror(errno));
            exit(errno);
        }
    }
    int status;
    wait(&status);
    for(int i = 0; i < 3; i++) {
        free(args[i]);
    }
    free(args);
}

void initialiseMutex() {
    mutex = (pthread_mutex_t*)calloc(1, sizeof(pthread_mutex_t));
    //атрибуты мьютекса
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    //устанавливаем тип мьютекса
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    //создаем мьютекс
    int status = pthread_mutex_init(mutex, &attr);
    if(status != 0) {
        printf("Error creating mutex: %s\n", strerror(status));
        exit(status);
    }
}

void startThreads(int numOfThreads, int blockSize) {
    threads = (pthread_t*)calloc(numOfThreads, sizeof(pthread_t));
    //поток с номером 0 - главный поток
    threads[0] = pthread_self();
    for (int i = 1; i < numOfThreads; i++) {
        ThreadInfo* info = (ThreadInfo*)calloc(1, sizeof(ThreadInfo));
        info->buffer = buffer;
        info->blockSize = blockSize;
        info->threadNum = i;
        int retVal = pthread_create(&threads[i], NULL, startThread, info);
        if (retVal != 0) {
            printf("Error starting %d thread: %s\n", i, strerror(retVal));
            exit(retVal);
        }
    }
}

void createBarrier(int numOfThreads) {
    //инициализация барьера с атрибутами по умолчанию
    if (pthread_barrier_init(&barrier, NULL, numOfThreads) != 0) {
        printf("Error initialising barrier: %s\n", strerror((errno)));
        exit(errno);
    }
}

void openAndMapFile(char* fileName, int memSize, int offset) {
    int fd = open(fileName, O_RDWR);
    if (fd == -1) {
        printf("Error opening file: %s\n", strerror(errno));
        exit(errno);
    }
    //отображаем memSize байт от начала сгенерированного файла в память, разрешая чтение и запись для разных процессов
    buffer = mmap(NULL, memSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
    if (buffer == (void*)-1) {
        printf("Error mapping file: %s\n", strerror(errno));
        exit(errno);
    }
    if (close(fd) == -1) {
        printf("Error closing file: %s\n", strerror(errno));
        exit(errno);
    }
}

void unmapFile(int memSize) {
    //удаление отображения и запись изменений в файл
    if (munmap(buffer, memSize) == -1) {
        printf("Error unmapping file: %s\n", strerror(errno));
        exit(errno);
    }
}

void cancelThreads(int numOfThreads) {
    //отменяем потоки
    for (int i = 1; i < numOfThreads; i++) {
        void *retVal;
        pthread_cancel(threads[i]);
        int status = pthread_join(threads[i], &retVal);
        //проверяем возвращаемое значение и проверяем, действительно ли завершился поток
        if (status != 0 || retVal != PTHREAD_CANCELED) {
            printf("Error cancelling thread %d (%lu): %s or %s\n", i + 1, threads[i], strerror(status),
                   strerror(*(int *) retVal));
            exit(EXIT_FAILURE);
        }
    }
    free(threads);
}

void sortFile(int numOfThreads, int memSize, char* fileName) {
    //карта отсортированных блоков
    blockMap = (int*)calloc(numOfBlocks, sizeof(int));
    //запустить потоки с 1 по threads - 1
    startThreads(numOfThreads, memSize / numOfBlocks);
    bufferIsReady = 1;
    //сортируем файл по частям
    for (int j = 0; j < 2 ; j++) {
        //первые threads блоков - заняты
        for (int i = 0; i < numOfThreads; i++) {
            blockMap[i] = 0;
        }
        //блоки, которые превосходят количество потоков - свободные
        for (int i = numOfThreads; i < numOfBlocks; i++) {
            blockMap[i] = 1;
        }
        //открыть и отобразить в память часть файла
        openAndMapFile(fileName, memSize, memSize * j);
        //сообщаем потокам о готовности буфера
        bufferIsReady = 1;
        //информация главного потока
        ThreadInfo info = {
                .blockSize = memSize / numOfBlocks,
                .buffer = buffer,
                .threadNum = 0
        };
        //синхронизация на барьере
        pthread_barrier_wait(&barrier);
        //переход к сортировке
        sortingPhase(&info);
        unmapFile(memSize);
    }
    cancelThreads(numOfThreads);
    //конечное слияние отсортированных частей файла
    //массив для хранения всех записей
    Index* merged = (Index*)calloc(indexHeader.records, sizeof(Index));
    //первая часть файла размером memSize
    Index* block1 = (Index*)calloc(memSize / sizeof(Index), sizeof(Index));
    //вторая часть файла размером memSize
    Index* block2 = (Index*)calloc(memSize / sizeof(Index), sizeof(Index));
    //отображаем в память первую половину файла
    openAndMapFile(fileName, memSize, 0);
    //копируем из нее данные
    memcpy(block1, buffer, memSize);
    unmapFile(memSize);
    //отображаем в память вторую половину файла
    openAndMapFile(fileName, memSize, memSize);
    //копируем из нее данные
    memcpy(block2, buffer, memSize);
    mergeSortedBlocks(block1, memSize / sizeof(Index), block2, memSize / sizeof(Index), merged);
    //копируем вторую часть обратно в файл
    memcpy(buffer, merged + (memSize / sizeof(Index)), memSize);
    unmapFile(memSize);
    //отображаем первую часть файла
    openAndMapFile(fileName, memSize, 0);
    //копируем первую часть обратно в файл
    memcpy(buffer, merged, memSize);
    unmapFile(memSize);
    free(merged);
    free(block1);
    free(block2);
    free(blockMap);
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        printf("Incorrect number of args.\nProgram has format: sort_index memsize blocks threads filenаme\n");
        exit(EXIT_FAILURE);
    }
    int memSize = strtol(argv[1], NULL, 10);
    if (memSize % getpagesize() != 0 || memSize % 256 != 0) {
        printf("The working buffer size must be a multiple of the page size = %d and must be divisible by 256 without remainder.\n", getpagesize());
        exit(EXIT_FAILURE);
    }
    numOfBlocks = strtol(argv[2], NULL, 10);
    if (numOfBlocks % 2 != 0 || numOfBlocks < NUM_OF_CORES * 4) {
        printf("The number of blocks must be a power of two and exceed the number of threads by at least 4 times.\n");
        exit(EXIT_FAILURE);
    }
    int numOfThreads = strtol(argv[3], NULL, 10);
    if (numOfThreads > NUM_OF_CORES * 8 || numOfThreads < NUM_OF_CORES) {
        printf("Number of threads should be in range from %d to %d\n", NUM_OF_CORES, NUM_OF_CORES * 8);
        exit(EXIT_FAILURE);
    }
    char* fileName = argv[4];
    //количество записей в файле
    indexHeader.records = (memSize * 2) / sizeof(Index);
    //инициализация барьера
    createBarrier(numOfThreads);
    initialiseMutex();
    while(1) {
        printf("\nChoose action:\n1. Generate file\n2. Sort generated file\n3. View file\n4. Exit\n");
        char option;
        scanf("%c", &option);
        while (getchar() != '\n') {}
        switch (option) {
            case '1':
                //генерация файла
                generateFile(fileName);
                break;
            case '2':
                sortFile(numOfThreads, memSize, fileName);
                printf("\nFile sorted successfully\n");
                break;
            case '3':
                displayFile(fileName);
                break;
            case '4': {
                //удаление мьютекса
                int retVal = pthread_mutex_destroy(mutex);
                if (retVal != 0) {
                    printf("Error destroying mutex: %s\n", strerror(retVal));
                    exit(retVal);
                }
                //удаление барьера
                if (pthread_barrier_destroy(&barrier) == -1) {
                    printf("Error destroying barrier: %s\n", strerror(errno));
                    exit(errno);
                }
                return 0;
            }
            default:
                printf("\nIncorrect input\n");
                break;
        }
    }
    return 0;
}