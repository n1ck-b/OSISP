#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <locale.h>

int compareStrings(const void* str1, const void* str2);
char** sortFilesNames(char** filesNames, int amountOfFiles);
void getFilesNames(char* sourceDirPath, char** options, char*** filesNames, int* amountOfFiles, char* dirPath);
