#include "generate.h"

int generateFile(int numOfRecords) {
    Record* records = (Record*)calloc(numOfRecords, sizeof(Record));
    srand(time(NULL));
    for (int i = 0; i < numOfRecords; i++) {
        records[i].id = rand() % RAND_MAX;
        records[i].semester = (rand() % 8) + 1;
        records[i].gpa = ((double)rand() / RAND_MAX) + rand() % 10;
    }
    //создаем и открываем файл для чтения/записи с правами чтения, записи и исполнения
    int fd = open(FILE_NAME,  O_CREAT | O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
    if (fd == -1) {
        printf("Ошибка создания/открытия файла: %s\n", strerror(errno));
        free(records);
        exit(errno);
    }
    //записываем массив структур в файл
    if (write(fd, records, sizeof(Record) * numOfRecords) < (ssize_t)(sizeof(Record) * numOfRecords)) {
        printf("Ошибка записи в файл: %s\n", strerror(errno));
        free(records);
        exit(errno);
    }
    free(records);
    return fd;
}