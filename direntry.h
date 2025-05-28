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
    ARCHIVE = 32
} Attributes;

typedef struct DirEntry {
    char name [255]; //имя файла
    unsigned short numOfFirstCluster; //номер кластера, с которого начинается файл
    unsigned short sizeInBytes; //размер файла в байтах
    unsigned char attributes; //атрибуты: 1 - только для чтения, 2 - скрытый файл, 4 - системный файл, 8 - метка тома, 16 - каталог, 32 - архив
    time_t creationTime; //время создания
    time_t modificationTime; //время последнего изменения
    time_t lastAccessTime; //время последнего доступа
    mode_t permissions; //права
} DirEntry;

#endif //COURSEWORK_DIRENTRY_H