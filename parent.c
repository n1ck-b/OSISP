#include "header.h"

extern char **environ;

int compareStrings(const void* str1, const void* str2)
{
    return strcoll(*(const char**)str1, *(const char**)str2);
}

void removeEnter(char** str)
{
    int len = strlen(*str);
    (*str)[len - 1] = '\0';
}

void getParentEnvVariables()
{
    int numOfEnvVariables = 0;
    while (environ[numOfEnvVariables] != NULL) 
    {
        ++numOfEnvVariables;
    }
    //копируем массив environ
    char** environCopy = (char**)calloc(numOfEnvVariables, sizeof(char*));
    memcpy(environCopy, environ, numOfEnvVariables * sizeof(char*));
    qsort(environCopy, numOfEnvVariables, sizeof(char*), compareStrings);
    printf("Переменные окружения программы parent:\n\n");
    for (int i = 0; i < numOfEnvVariables; ++i) 
    {
        printf("%s\n", environCopy[i]);
    }
}

char** getChildEnvVariables(int *numOfChildEnvVariables) 
{
    FILE* envFile = fopen("/home/nikita/Labs/lab2/.env", "r");
    if (envFile == NULL) 
    {
        printf("Ошибка открытия файла env\n");
        exit(EXIT_FAILURE);
    }
    char** childEnvVariables = (char**)calloc(1, sizeof(char*));
    *childEnvVariables = (char*)calloc(1000, sizeof(char));
    while (fgets(childEnvVariables[*numOfChildEnvVariables], 100, envFile) != NULL)
    {
        //выделение памяти под новую строку
        (*numOfChildEnvVariables)++;
        childEnvVariables = (char **)realloc(childEnvVariables, sizeof(char *) * (*numOfChildEnvVariables + 1));
        childEnvVariables[*numOfChildEnvVariables] = (char*)calloc(1000, sizeof(char));
    }
    childEnvVariables[*numOfChildEnvVariables] = (char*)0;
    fclose(envFile); 
    return childEnvVariables;
}

char** getValuesOfChildEnvVariables(int numOfChildEnvVariables, char** childEnvVariables)
{
    char* temp; 
    for (int i = 0; i < numOfChildEnvVariables; i++) 
    {
        removeEnter(&(childEnvVariables[i]));
        temp = getenv(childEnvVariables[i]);
        if (temp == NULL) 
        {
            printf("Ошибка получения значения для переменной окружения %s\n", childEnvVariables[i]);
            exit(EXIT_FAILURE);
        }
        strcat(childEnvVariables[i], "=");
        strcat(childEnvVariables[i], temp);
    }
    return childEnvVariables;
}

void startChildProcess(int* numOfChildProcesses, char* childPath, int option, char** childEnvVariables)
{
    char seqNum[3];
    char* temp;
    char** args = (char**)calloc(3, sizeof(char*));
    for (int i = 0; i < 3; i++) 
    {
        args[i] = (char*)calloc(30, sizeof(char));
    }
    //перевод порядкового номера дочернего процесса в строку
    snprintf(seqNum, sizeof(temp), "%d", *numOfChildProcesses);
    strcpy(args[0], "child_");
    args[0] = strcat(args[0], seqNum);
    if (option == '+')
        strcpy(args[1], "/home/nikita/Labs/lab2/.env");
    if (option == '*')
        args[1] = NULL;
    args[2] = (char*)0;
    (*numOfChildProcesses)++;
    pid_t pid = fork();
    if (pid == -1) 
    {
        printf("Возникла ошибка при создании дочернего процесса, код ошибки - %d\n", errno);
        exit(errno);
    }
    if (pid == 0) //ветка дочернего процесса
    {
        printf("Запуск дочернего процесса\n");
        if(execve(childPath, args, childEnvVariables) == -1)
        {
            printf("Ошибка запуска программы child, код ошибки: %d\n", errno);
            exit(errno);
        }
    }
}
int main(int argc, char* argv[])
{
    setlocale(LC_COLLATE, "C");
    getParentEnvVariables();
    int numOfChildEnvVariables = 0;
    char **childEnvVariables = getChildEnvVariables(&numOfChildEnvVariables);
    childEnvVariables = getValuesOfChildEnvVariables(numOfChildEnvVariables, childEnvVariables);
    char* childPath = getenv("CHILD_PATH");
    if (childPath == NULL) 
    {
        printf("Ошибка: не найдена переменная окружения CHILD_PATH\n");
        exit(EXIT_FAILURE);
    }
    int option;
    int numOfChildProcesses = 0;
    int childStatus;
    while (1) 
    {
        printf("\nВыберите одну из следующих опций: '+', '*', 'q'\n");
        option = getchar();
        //очистка буфера ввода
        while (getchar() != '\n') {}
        if (option == '+' || option == '*') 
        {
            startChildProcess(&numOfChildProcesses, childPath, option, childEnvVariables);
            wait(&childStatus);
        }
        else if (option == 'q') 
        {
            wait(&childStatus);
            printf("Дочерний процесс завершился с кодом %d\n", childStatus);
            printf("Завершение родительского процесса\n");
            exit(0);
        }
        else
            printf("Введена неверная опция\n");
    }
    return 0;
}
