#ifndef COURSEWORK_DIRENTRY_H
#define COURSEWORK_DIRENTRY_H
#include <time.h>
#include <sys/stat.h>

typedef enum {
    ONLY_FOR_READ = 1,
    HIDDEN = 2,
    SYSTEM = 4,
    VOLUME_LABEL = 8,
    DIRECTORY = 16,
    ARCHIVE = 20
} Attributes;

typedef struct DirEntry {
    char name [255]; //имя файла
    unsigned short numOfFirstCluster; //номер кластера, с которого начинается файл
    int sizeInBytes; //размер файла в байтах
    Attributes attributes; //атрибуты
    struct tm creationTime; //время создания
    struct tm modificationTime; //время последнего изменения
    mode_t permissions; //права
} DirEntry;

#endif //COURSEWORK_DIRENTRY_H