#include "gen.h"

int generateFile(const char* fileName, size_t records) {
    Index indexes[records];
    int integerPart;
    double fractionalPart;
    //установка новой последовательности случайных чисел
    srand(time(NULL));
    for (size_t i = 0; i < records; i++) {
        integerPart = (rand() % (MODIFIER_JD - 15019)) + 15020;
        fractionalPart = (double)rand() / (RAND_MAX + 1.0);
        indexes[i].timeMark = integerPart + fractionalPart;
        indexes[i].recNum = i + 1;
    }
    FILE* generatedFile = fopen(fileName, "w+b");
    if (generatedFile == NULL) {
        return -1;
    }
    if (fwrite(indexes, sizeof(Index), records, generatedFile) < records) {
        fclose(generatedFile);
        return -1;
    }
    fclose(generatedFile);
    return 0;
}

int main(int argc, char* argv[]) {
    int records = strtol(argv[0], NULL, 10);
    if (generateFile(argv[1], records) == -1) {
        printf("Error generating file\n");
        exit(EXIT_FAILURE);
    }
    return 0;
}