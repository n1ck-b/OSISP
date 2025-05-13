#ifndef LAB7_GENERATE_H
#define LAB7_GENERATE_H

#define FILE_NAME "students"

#include "header.h"

int generateFile(int numOfRecords);

typedef struct {
    uint32_t id; //ID студента
    uint8_t semester; //номер семестра
    double gpa; //средний балл
} Record;

#endif //LAB7_GENERATE_H
