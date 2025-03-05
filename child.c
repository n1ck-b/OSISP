#include "header.h"

char** getEnvVariables(int* numOfEnvVariables, char* argv[])
{
    FILE* envFile = fopen(argv[1], "r");
    if (envFile == NULL) 
    {
        printf("Ошибка открытия файла env\n");
        exit(EXIT_FAILURE);
    }
    char** envVariables = (char**)calloc(1, sizeof(char*));
    *envVariables = (char*)calloc(1000, sizeof(char));
    while (fgets(envVariables[*numOfEnvVariables], 100, envFile) != NULL) 
    {
        //выделение памяти под новую строку и удаление символа '\n'
        envVariables[*numOfEnvVariables][strlen(envVariables[*numOfEnvVariables]) - 1] = '\0';
        (*numOfEnvVariables)++;
        envVariables = (char**)realloc(envVariables, sizeof(char*) * (*numOfEnvVariables + 1));
        envVariables[*numOfEnvVariables] = (char*)calloc(1000, sizeof(char));
    }
    envVariables = (char**)realloc(envVariables, (*numOfEnvVariables - 1)*sizeof(char*));
    if(fclose(envFile) == EOF)
    {
        printf("Ошибка закрытия env файла\n");
        exit(EXIT_FAILURE);
    }
    return envVariables;
}
char** getValuesOfEnvVariables(int numOfEnvVariables, char** envVariables)
{
    char *temp;
    for (int i = 0; i < numOfEnvVariables; i++) 
    {
        temp = getenv(envVariables[i]);
        if (temp == NULL) 
        {
            printf("Ошибка получения значения для переменной окружения %s\n", envVariables[i]);
            exit(EXIT_FAILURE);
        }
        strcat(envVariables[i], "=");
        strcat(envVariables[i], temp);
    }
    return envVariables;
}

int main(int argc, char* argv[], char* envp[])
{
    printf("Имя дочернего процесса: %s\n", argv[0]);
    printf("pid процесса: %d\n", getpid());
    printf("ppid процесса: %d\n", getppid());
    if (argc > 1) //случай, когда была введен +
    {
        int numOfEnvVariables = 0;
        char **envVariables = getEnvVariables(&numOfEnvVariables, argv);
        envVariables = getValuesOfEnvVariables(numOfEnvVariables, envVariables);
        printf("Параметры окружения дочернего процесса:\n");
        for (int i = 0; i < numOfEnvVariables; i++) 
        {
            printf("%s\n", envVariables[i]);
        }
    }
    else //случай, когда была введена *
    {
        printf("Параметры окружения дочернего процесса:\n");
        int numOfEnvVariables = 0;
        while (envp[numOfEnvVariables] != NULL) 
        {
            printf("%s\n", envp[numOfEnvVariables]);
            numOfEnvVariables++;
        }
    }
    printf("Завершение дочернего процесса %s\n", argv[0]);
    return 0;
}

