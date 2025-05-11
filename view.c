#include "view.h"

void displayFile(char* fileName, size_t numOfRecords) {
    FILE* file = fopen(fileName, "rb");
    if (file == NULL) {
        printf("Error opening file %s for reading: %s\n", fileName, strerror(errno));
        exit(errno);
    }
    Index* records = (Index*)calloc(numOfRecords, sizeof(Index));
    if (fread(records, sizeof(Index), numOfRecords, file) < numOfRecords) {
        printf("Error reading from file %s: %s\n", fileName, strerror(errno));
        exit(errno);
    }
    printf("\nFile contents:\n\n");
    for (size_t i = 0; i < numOfRecords; i++) {
        printf("%lu\t%f\n", records[i].recNum, records[i].timeMark);
    }
    free(records);
    fclose(file);
}

int main(int argc, char* argv[]) {
    int records = strtol(argv[0], NULL, 10);
    displayFile(argv[1], records);
    return 0;
}