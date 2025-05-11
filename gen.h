#ifndef LAB6_GEN_H
#define LAB6_GEN_H

#include "sort_index.h"

//модификатор дня 10.05.2025
#define MODIFIER_JD 60805

typedef struct {
    double timeMark; // временная метка (модифицированная юлианская дата)
    uint64_t recNum; // номер записи в таблице БД (первичный индекс)
} Index;

typedef struct {
    uint64_t records; // количество записей
    Index* indexes; // массив записей в количестве records
} IndexHeader;

#endif //LAB6_GEN_H