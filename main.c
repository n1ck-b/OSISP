#include "header.h"

void printRecords(Record* records, int numOfRecords, int* recordsNums) {
    printf("\n+----+-------------+----------+----------+\n");
    printf("| №  | ID студента | Семестр  |  Ср.балл |\n");
    printf("+----+-------------+----------+----------+\n");
    if (recordsNums == NULL) {
        for (int i = 0; i < numOfRecords; i++) {
            printf("| %-3d| %10u  | %-8u | %.2f     |\n",
                   i + 1, records[i].id, records[i].semester, records[i].gpa);
        }
    }
    else {
        for (int i = 0; i < numOfRecords; i++) {
            printf("| %-3d| %10u  | %-8u | %.2f     |\n",
                   recordsNums[i], records[i].id, records[i].semester, records[i].gpa);
        }
    }
    printf("+----+-------------+----------+----------+\n");
}

int generateOrOpenFile() {
    int fd;
    char option;
    while(1) {
        printf("\nВыберите действие с файлом:\n 1. Сгенерировать новый\n 2. Открыть существующий\n");
        scanf("%c", &option);
        while (getchar() != '\n') {}
        switch (option) {
            case '1':
                //генерируем файл из 20 записей
                fd = generateFile(NUM_OF_RECORDS);
                return fd;
            case '2':
                //создаем и открываем файл для чтения/записи с правами чтения, записи и исполнения
                fd = open(FILE_NAME,  O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
                if (fd == -1) {
                    printf("Ошибка открытия файла: %s.\n", strerror(errno));
                    break;
                }
                return fd;
            default:
                printf("Некорректная опция\n");
                break;
        }
    }
}

Record* readFromFile(int fd, int numOfRecords, int recordNum, int wait) {
    struct flock fl;
    int offset = 0;
    fl.l_type = F_RDLCK; //блокировка на чтение, чтобы запретить другим процессам возможность установки блокировки на запись
    fl.l_pid = 0;
    fl.l_whence = SEEK_SET;
    if (numOfRecords == NUM_OF_RECORDS) {
        //блокируем весь файл
        fl.l_start = 0;
        fl.l_len = 0;
    }
    else {
        //блокируем часть файла, в которую входят нужные нам записи
        fl.l_start = (recordNum - 1) * sizeof(Record);
        fl.l_len = sizeof(Record) * numOfRecords;
        //задаем нужное смещение от начала файла
        offset = (recordNum - 1) * sizeof(Record);
    }
    //установка блокировки
    if (fcntl(fd, F_OFD_SETLK, &fl) == -1) {
        if (errno == EAGAIN || errno == EACCES) {
            printf("\033[31m\nДоступ к файлу заблокирован\033[0m\n");
            return NULL;
        }
        else {
            printf("Ошибка блокировки файла на чтение: %s\n", strerror(errno));
            exit(errno);
        }
    }
    if(wait) {
        printf("\nНажмите любую клавишу");
        getchar();
    }
    //перемещаем указатель на нужную позицию в файле
    if (lseek(fd, offset, SEEK_SET) == -1) {
        printf("Ошибка чтения файла: %s\n", strerror(errno));
        exit(errno);
    }
    //массив записей
    Record* records = (Record*)calloc(numOfRecords, sizeof(Record));
    //чтение из файла
    if (read(fd, records, numOfRecords * sizeof(Record)) < (ssize_t)(numOfRecords * sizeof(Record))) {
        printf("Ошибка чтения файла. Слишком мало данных или %s\n", strerror(errno));
        free(records);
        exit(EXIT_FAILURE);
    }
    //снятие блокировки
    fl.l_type = F_UNLCK;
    if (fcntl(fd, F_OFD_SETLK, &fl) == -1) {
        printf("Ошибка снятия блокировки: %s\n", strerror(errno));
        free(records);
        exit(errno);
    }
    return records;
}

int getRecordNum() {
    printf("\nВведите порядковый номер записи\n");
    int recordNum;
    int retVal = scanf("%d", &recordNum);
    while (getchar() != '\n') {}
    if (recordNum <= 0 || recordNum > NUM_OF_RECORDS || retVal != 1) {
        printf("Неверный порядковый номер\n");
        return -1;
    }
    return recordNum;
}

Record modifyRecord(Record record) {
    char option;
    while(1) {
        int retVal = 0;
        printf("\nВыберите поле для изменения:\n 1. ID\n 2. Семестр\n 3. Средний балл\n 4. Закончить изменение\n");
        scanf("%c", &option);
        while (getchar() != '\n') {}
        switch (option) {
            case '1':
                printf("\nВведите новое значение ID от 1 до %d\n", RAND_MAX - 1);
                uint32_t id;
                retVal = scanf("%u", &id);
                while (getchar() != '\n') {}
                if (id <= 0 || id >= RAND_MAX || retVal != 1){
                    printf("Некорректное значение ID\n");
                    break;
                }
                record.id = id;
                break;
            case '2':
                printf("\nВведите новое значение номера семестра от 1 до 8\n");
                unsigned short semester;
                retVal = scanf("%hu", &semester);
                while (getchar() != '\n') {}
                if (semester <= 0 || semester > 8 || retVal != 1) {
                    printf("Некорректное значение номера семестра\n");
                    break;
                }
                record.semester = semester;
                break;
            case '3':
                printf("\nВведите новое значение среднего балла от 0.00 до 10.00\n");
                double gpa;
                retVal = scanf("%lf", &gpa);
                while (getchar() != '\n') {}
                if (gpa < 0 || gpa > 10 || retVal != 1) {
                    printf("Некорректное значение среднего балла\n");
                    break;
                }
                record.gpa = gpa;
                break;
            case '4':
                return record;
            default:
                printf("\nНеверная опция\n");
                break;
        }
    }
}

void saveRecordToFile(int fd, Record* record, int recordNum) {
    int offset = (recordNum - 1) * sizeof(Record);
    //перемещаем указатель на нужную позицию в файле
    if (lseek(fd, offset, SEEK_SET) == -1) {
        printf("Ошибка записи в файл: %s\n", strerror(errno));
        exit(errno);
    }
    //сохраняем запись в файл
    if (write(fd, record, sizeof(Record)) < (ssize_t)sizeof(Record)) {
        printf("Ошибка записи в файл: %s\n", strerror(errno));
        exit(errno);
    }
}

Record checkUpdatesAndSaveRecord(int fd, int lastModifiedRecordNum, Record lastReadRecord, Record lastModifiedRecord) {
    //блокируем запись для модификации в файле
    struct flock fl;
    fl.l_type = F_WRLCK; //блокировка на запись, чтобы запретить другим процессам возможность изменять эту запись
    fl.l_pid = 0;
    fl.l_whence = SEEK_SET;
    //блокируем нужную запись
    fl.l_start = (lastModifiedRecordNum - 1) * sizeof(Record);
    fl.l_len = sizeof(Record);
    //установка блокировки
    if (fcntl(fd, F_OFD_SETLK, &fl) == -1) {
        if (errno == EAGAIN || errno == EACCES) {
            printf("\033[31m\nДоступ к файлу заблокирован\033[0m\n");
            return lastReadRecord;
        }
        else {
            printf("Ошибка блокировки файла на запись: %s\n", strerror(errno));
            exit(errno);
        }
    }
    printf("\nНажмите любую клавишу");
    getchar();
    //перечитываем запись из файла
    Record* recordFromFile = readFromFile(fd, 1, lastModifiedRecordNum, 0);
    //если запись была изменена другим процессом после получения ее текущим процессом
    if (lastReadRecord.gpa != recordFromFile->gpa || lastReadRecord.semester != recordFromFile->semester || lastReadRecord.id != recordFromFile->id) {
        //снимаем блокировку с нужной записи
        fl.l_type = F_UNLCK;
        if (fcntl(fd, F_OFD_SETLK, &fl) == -1) {
            printf("Ошибка снятия блокировки: %s\n", strerror(errno));
            free(recordFromFile);
            exit(errno);
        }
        printf("\033[31m\nЗапись с порядковым номером %d была изменена в другом процессе. Невозможно сохранить изменения\033[0m\n", lastModifiedRecordNum);
        //сохраняем новую запись
        lastReadRecord = *recordFromFile;
        free(recordFromFile);
        return lastReadRecord;
    }
    //сохраняем изменения
    saveRecordToFile(fd, &lastModifiedRecord, lastModifiedRecordNum);
    //снимаем блокировку с нужной записи
    fl.l_type = F_UNLCK;
    if (fcntl(fd, F_OFD_SETLK, &fl) == -1) {
        printf("Ошибка снятия блокировки: %s\n", strerror(errno));
        free(recordFromFile);
        exit(errno);
    }
    free(recordFromFile);
    //сохраняем изменения в переменной, отвечающей за проверки
    lastReadRecord = lastModifiedRecord;
    printf("\033[32m\nИзменения успешно сохранены\033[0m\n");
    return lastReadRecord;
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru");
    char option;
    //открываем существующий или генерируем новый файл
    int fd = generateOrOpenFile();
    Record lastModifiedRecord = {0,0,0};
    Record lastReadRecord;
    int lastModifiedRecordNum = 0;
    while(1) {
        printf("\nВыберите действие:\n 1. Отображение содержимого файла с последовательной нумерацией записей\n 2. Получение записи по порядковому номеру\n 3. Модификация полей записи\n 4. Сохранение последней прочитанной и модифицированной записи\n 5. Выход\n");
        scanf("%c", &option);
        while (getchar() != '\n') {}
        switch(option) {
            case '1': {
                Record* records = readFromFile(fd, NUM_OF_RECORDS, 0, 1);
                if (records != NULL) {
                    printRecords(records, NUM_OF_RECORDS, NULL);
                    free(records);
                }
                break;
            }
            case '2': {
                //номер записи для чтения
                int recordNum = getRecordNum();
                if (recordNum == -1)
                    break;
                Record* record = readFromFile(fd, 1, recordNum, 1);
                if (record != NULL) {
                    int recordsNums[1];
                    recordsNums[0] = recordNum;
                    printRecords(record, 1, recordsNums);
                    free(record);
                }
                break;
            }
            case '3': {
                //номер записи для чтения
                int recordNum = getRecordNum();
                if (recordNum == -1)
                    break;
                //получение записи по номеру
                Record* record = readFromFile(fd, 1, recordNum, 0);
                if (record == NULL)
                    break;
                lastReadRecord = *record;
                lastModifiedRecord = modifyRecord(*record);
                lastModifiedRecordNum = recordNum;
                free(record);
                break;
            }
            case '4':
                //если не была изменена ни одна запись
                if (lastModifiedRecordNum == 0) {
                    printf("Не было изменено ни одной записи\n");
                    break;
                }
                //если запись была изменена
                if (lastReadRecord.gpa != lastModifiedRecord.gpa || lastReadRecord.semester != lastModifiedRecord.semester || lastReadRecord.id != lastModifiedRecord.id) {
                    lastReadRecord = checkUpdatesAndSaveRecord(fd, lastModifiedRecordNum, lastReadRecord, lastModifiedRecord);
                    break;
                }
                printf("\nНет изменений для сохранения\n");
                break;
            case '5':
                if (close(fd) == -1) {
                    printf("Ошибка закрытия файла: %s\n", strerror(errno));
                    exit(errno);
                }
                return 0;
            default:
                printf("Неверная опция\n");
                break;
        }
    }
    return 0;
}