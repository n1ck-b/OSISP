/*#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <locale.h>*/
#include "header.h"

int compareStrings(const void* str1, const void* str2)
{
    //return strcasecmp(*(const char**)str1, *(const char**)str2);
    return strcoll(*(const char**)str1, *(const char**)str2);
}

char** sortFilesNames(char** filesNames, int amountOfFiles)
{
    qsort(filesNames, amountOfFiles, sizeof(char*), compareStrings);
    return filesNames;
}

void getFilesNames(char* sourceDirPath, char** options, char*** filesNames, int* amountOfFiles, char* dirPath)
{
    DIR* currentDir = opendir(dirPath);
    if (currentDir == NULL) 
    {
        //printf("Error opening the directory '%s': %s\n", dirPath, strerror(errno));
        printf("dirwalk: '%s': %s\n", dirPath, strerror(errno));
        return;
    }
    struct dirent *currentFile;
    char* fileName = (char*)calloc(sizeof(char), 150);
    char* filePath = (char*)calloc(sizeof(char), PATH_MAX);
    struct stat statBuffer;
    while ((currentFile = readdir(currentDir))!= NULL) 
    {
        strcpy(filePath, dirPath);
        strcat(filePath, "/");
        strcat(filePath, currentFile->d_name);
        lstat(filePath, &statBuffer);
        if ((strstr(*options, "l") && S_ISLNK(statBuffer.st_mode)) || (strstr(*options, "d") && S_ISDIR(statBuffer.st_mode) && strcmp(currentFile->d_name, ".") != 0 && strcmp(currentFile->d_name, "..") != 0) || (strstr(*options, "f") && S_ISREG(statBuffer.st_mode)))
        {
            char* relativePath = (char*)calloc(sizeof(char), PATH_MAX);
            char *tmp = relativePath;
            //копируем строку filePath, где содержится абсолютный путь к файлу, в строку relativePath
            strcpy(relativePath, filePath);
            //перемещаем указатель с начала строки на длину пути к каталогу, где была вызвана программа
            relativePath = relativePath + strlen(sourceDirPath) - 1;
            relativePath[0] = '.';
            /*if ((strcmp(currentFile->d_name, ".") == 0 || strcmp(currentFile->d_name, "..") == 0) && *amountOfFiles < 2)
            {
                relativePath += 2;
            }*/
            strcpy((*filesNames)[*amountOfFiles], relativePath);
            (*amountOfFiles)++;
            //выделение памяти под новую строку
            *filesNames = (char **)realloc(*filesNames, sizeof(char *) * (*amountOfFiles + 1));
            //выделение памяти под 250 символов в новой строке
            (*filesNames)[*amountOfFiles] = (char*)calloc(sizeof(char), 250);
            //перевыделение памяти в последней заполненной строке
            (*filesNames)[*amountOfFiles - 1] = (char*)realloc((*filesNames)[*amountOfFiles - 1], sizeof(char)* (strlen((*filesNames)[*amountOfFiles - 1])+ 1));
            free(tmp);
        }
        if (S_ISDIR(statBuffer.st_mode) && strcmp(currentFile->d_name, ".") != 0 && strcmp(currentFile->d_name, "..") != 0)
        {
            getFilesNames(sourceDirPath, options, filesNames, amountOfFiles, filePath);
        }
    }
    closedir(currentDir);
    free(fileName);
    free(filePath);
}

int main(int argc, char* argv[])
{
    setlocale(LC_COLLATE, "ru_RU.UTF-8");
    char* options = (char*)calloc(sizeof(char), 4);
    int numOfOptions = 0;
    char* path = (char*)calloc(sizeof(char), 200);
    int res;
    while ((res = getopt(argc, argv, "ldfs"))!= -1)
    {
        options[numOfOptions] = res;
        numOfOptions++;
    }
    if (strstr(options, "?")) 
    {
        fprintf(stderr, "Usage: %s [dir] [options]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int size = numOfOptions;
    if (numOfOptions == 0 || strcmp(options, "s") == 0) 
    {
        strcat(options, "ldf");
        size = 3;
    }
    options = (char*)realloc(options, sizeof(char)*size);
    //если не указан каталог или если он указан как '.'
    if (argc == numOfOptions + 1 || strcmp(argv[numOfOptions+1],".") == 0)
        getcwd(path, 200);
    else
        strcpy(path, argv[numOfOptions+1]);
    char** filesNames = (char**)calloc(sizeof(char*), 1);
    *filesNames = (char*)calloc(sizeof(char), 250);
    int amountOfFiles = 0;
    getFilesNames(path, &options, &filesNames, &amountOfFiles, path);
    if (strstr(options, "s"))
    {
        filesNames = sortFilesNames(filesNames, amountOfFiles);
    }
    for (int i = 0;i < amountOfFiles; i++)
    {
        printf("%s\n", filesNames[i]);
    }
    for (int i=0;i < amountOfFiles; i++) 
    {
        free(filesNames[i]);
    }
    free(filesNames);
    free(options);
    return 0;
}

