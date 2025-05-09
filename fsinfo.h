#ifndef COURSEWORK_FSINFO_H
#define COURSEWORK_FSINFO_H

typedef struct FSInfo {
    int numOfFreeClusters; //число свободных кластеров
    int numOfFirstFreeCluster; //номер первого свободного кластера
    int numOfTotalClusters; //общее число кластеров
    unsigned short sizeOfClusterInBytes; //размер кластера в байтах
    int sizeOfFatInBytes; //размер таблицы FAT в байтах
    unsigned short startOfFat; //номер байта, с которого начинается FAT
    unsigned startOfData; //номер байта, с которого начинается блок данных
} FSInfo;

#endif //COURSEWORK_FSINFO_H