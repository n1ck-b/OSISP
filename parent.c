#include "header.h"

int childProcessIsFinished = 1;

void sigUsr2Handler(int signum)  
{
    childProcessIsFinished = 1;
}

void createChildProcess(pid_t **pids, int *numOfChildProcesses) 
{
    (*pids)[*numOfChildProcesses] = fork();
    if ((*pids)[*numOfChildProcesses] == -1)
    {
        printf("Возникла ошибка при создании дочернего процесса: %d\n", errno);
        (*numOfChildProcesses)--;
    }
    else if ((*pids)[*numOfChildProcesses] != 0) //ветка родительского процесса
    {
        printf("\nПорожден дочерний процесс: pid = %d. Кол-во дочерних процессов: %d\n", (*pids)[*numOfChildProcesses], (*numOfChildProcesses) + 1);
        (*numOfChildProcesses)++;
        //выделяем память для нового pid
        *pids = (pid_t *)realloc(*pids, sizeof(pid_t) * (*numOfChildProcesses + 1));
        childProcessIsFinished = 0;
    }
    else if ((*pids)[*numOfChildProcesses] == 0) //ветка дочернего процесса
    {
        execve("./child", NULL, NULL);
    }
}

void deleteLastSpawnedProcess(pid_t **pids, int *numOfChildProcesses, int* childStatus)
{
    if (*numOfChildProcesses == 0) 
    {
        printf("Дочерних процессов нет\n");
        return;
    }
    if (kill((*pids)[*numOfChildProcesses - 1], SIGUSR1) == -1) //отправляем сигнал дочернему процессу
    {
        printf("Ошибка удаления последнего порожденного дочернего процесса: %d\n", errno);
    }
    else
    {
        wait(childStatus);
        printf("\nПоследний порожденный дочерний процесс удален: pid = %d, статус: %d\n", (*pids)[*numOfChildProcesses - 1], *childStatus);
        (*numOfChildProcesses)--;
        printf("Количество оставшихся дочерних процессов: %d\n", *numOfChildProcesses);
        //удаляем pid удаленного дочернего процесса
        if (*numOfChildProcesses != 0)
            *pids = (pid_t *)realloc(*pids, sizeof(pid_t) * (*numOfChildProcesses));
    }
}

void printAllProcesses(pid_t *pids, int numOfChildProcesses) 
{
    printf("Родительский процесс: %d\n", getpid());
    printf("Дочерние процессы:\n");
    for (int i = 0; i < numOfChildProcesses; i++) 
    {
        printf("pid = %d\n", pids[i]);
    }
}

void deleteAllChildProcesses(pid_t **pids, int *numOfChildProcesses, int* childStatus) 
{
    if (*numOfChildProcesses == 0) 
    {
        printf("Дочерних процессов нет\n");
        return;
    }
    int n = *numOfChildProcesses;
    for (int i = 0; i < n; i++)
    {
        if(kill((*pids)[i], SIGUSR1) == -1)
        {
            printf("Ошибка удаления дочернего процесса с pid = %d, код ошибки: %d\n", (*pids)[i], errno);
        }
        else
        {
            wait(childStatus);
            (*numOfChildProcesses)--;
            printf("\nДочерний процесс успешно удален: pid = %d, статус: %d\n", (*pids)[i], *childStatus);
        }
    }
    *pids = (pid_t *)realloc(*pids, sizeof(pid_t));
}

int main(int argc, char* argv[])
{
    setlocale(LC_COLLATE, "C");
    char option;
    int numOfChildProcesses = 0;
    int childStatus;
    pid_t* pids = (pid_t*)calloc(1, sizeof(pid_t));
    struct sigaction actionForSigUsr2;
    //установка функции-обработчика
    actionForSigUsr2.sa_handler = sigUsr2Handler;
    //установка пустого набора заблокированных сигналов
    sigemptyset(&actionForSigUsr2.sa_mask);
    //сбрасываем флаги
    actionForSigUsr2.sa_flags= 0;
    //установка обработчика для сигнала SIGUSR2 
    sigaction(SIGUSR2, &actionForSigUsr2, NULL);
    while (1)
    {
        if (childProcessIsFinished) //если последний порожденный дочерний процесс завершил свои действия
        {
            printf("\nВыберите одну из следующих опций: '+', '-', 'l', 'k', 'q'\n");
            option = getchar();
            //очистка буфера ввода
            while (getchar() != '\n') {}
            if (option == '+') 
            {
                createChildProcess(&pids, &numOfChildProcesses);
            }
            else if (option == '-') 
            {
                deleteLastSpawnedProcess(&pids, &numOfChildProcesses, &childStatus);
            }
            else if (option == 'l') 
            {
                printAllProcesses(pids, numOfChildProcesses);
            }
            else if (option == 'k' || option == 'q') 
            {
                deleteAllChildProcesses(&pids, &numOfChildProcesses, &childStatus);
                if (option == 'q') 
                {
                    printf("Завершение родительского процесса\n");
                    exit(0);
                }
            }
            else
                printf("Введена неверная опция\n");
        }
        
    }
    return 0;
}
