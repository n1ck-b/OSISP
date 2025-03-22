#include "header.h"

#define _POSIX_C_SOURCE 200809L

typedef void (*sighandler_t)(int);
sighandler_t sa_handler;

struct twoNumbers {
    int first;
    int second;
} numbers;

struct statistics {
    int twoZeros;
    int zeroAndOne;
    int oneAndZero;
    int twoOnes;
} stats = {0, 0, 0, 0};

int signalWasReceived = 0;

void alarmHandler(int signum)
{
    if (numbers.first == 0 && numbers.second == 0) 
    {
        stats.twoZeros++;
    }
    else if (numbers.first == 0 && numbers.second == 1) 
    {
        stats.zeroAndOne++;
    }
    else if (numbers.first == 1 && numbers.second == 0) 
    {
        stats.oneAndZero++;
    }
    else if (numbers.first == 1 && numbers.second == 1) 
    {
        stats.twoOnes++;
    }
    signalWasReceived = 1;
}

void sigUsr1Handler(int signum)
{
    raise(SIGTERM);
}

void childProcess() 
{
    struct timespec time;
    time.tv_nsec = 1000000;
    struct twoNumbers zeros = {0, 0};
    struct twoNumbers ones = {1, 1};
    //обработка сигнала SIGALRM
    struct sigaction newActionForAlarm;
    newActionForAlarm.sa_handler = alarmHandler;
    sigemptyset(&newActionForAlarm.sa_mask);
    newActionForAlarm.sa_flags= 0;
    sigaction(SIGALRM, &newActionForAlarm, NULL);
    //обработка сигнала SIGUSR1
    struct sigaction newActionForSigUsr1;
    newActionForSigUsr1.sa_handler = sigUsr1Handler;
    sigemptyset(&newActionForSigUsr1.sa_mask);
    newActionForSigUsr1.sa_flags= 0;
    sigaction(SIGUSR1, &newActionForSigUsr1, NULL);
    for (int i = 0; i < 100; ++i) 
    {
        nanosleep(&time, NULL);
        signalWasReceived = 0;
        while (1) 
        {
            numbers = zeros;
            numbers = ones;
            if (signalWasReceived)
            {
                break;
            }
        }
    }
    printf("ppid = %d, pid = %d, {0, 0} = %d, {0, 1} = %d, {1, 0} = %d, {1, 1} = %d\n", getppid(), getpid(), stats.twoZeros, stats.zeroAndOne, stats.oneAndZero, stats.twoOnes);
}

int main(int argc, char* argv[])
{
    setlocale(LC_COLLATE, "C");
    char option;
    int numOfChildProcesses = 0;
    int childStatus;
    pid_t* pids = (pid_t*)calloc(1, sizeof(pid_t));
    while (1) 
    {
        printf("\nВыберите одну из следующих опций: '+', '-', 'l', 'k', 'q'\n");
        option = getchar();
        //очистка буфера ввода
        while (getchar() != '\n') {}
        if (option == '+') 
        {
            pids[numOfChildProcesses] = fork();
            if (pids[numOfChildProcesses] == -1)
            {
                printf("Возникла ошибка при создании дочернего процесса: %d\n", errno);
                numOfChildProcesses--;
            }
            else if (pids[numOfChildProcesses] != 0 /*&& pid[numOfChildProcesses] != -1*/) //ветка родительского процесса
            {
                printf("Порожден дочерний процесс: pid = %d. Кол-во дочерних процессов: %d\n", pids[numOfChildProcesses], numOfChildProcesses + 1);
                numOfChildProcesses++;
                pids = (pid_t *)realloc(pids, sizeof(pid_t) * (numOfChildProcesses + 1));
            }
            else if (pids[numOfChildProcesses] == 0) //ветка дочернего процесса
            {
                childProcess();
            }
        }
        else if (option == '-') 
        {
            if (kill(pids[numOfChildProcesses - 1], SIGUSR1) == -1)
            {
                printf("Ошибка удаления последнего порожденного дочернего процесса: %d\n", errno);
            }
            else
            {
                printf("Последний порожденный дочерний процесс удален: pid = %d\n", pids[numOfChildProcesses - 1]);
                numOfChildProcesses--;
                printf("Количество оставшихся дочерних процессов: %d", numOfChildProcesses);
                pids = (pid_t *)realloc(pids, sizeof(pid_t) * numOfChildProcesses);
            }
        }
        else if (option == 'l') 
        {
            printf("Родительский процесс: %d\n", getpid());
            printf("Дочерние процессы:\n");
            for (int i = 0; i < numOfChildProcesses; i++) 
            {
                printf("pid = %d\n", pids[i]);
            }
        }
        else if (option == 'k' || option == 'q') 
        {
            int n = numOfChildProcesses;
            for (int i = 0; i < n; i++) 
            {
                if(kill(pids[i], SIGUSR1) == -1)
                {
                    printf("Ошибка удаления дочернего процесса с pid = %d, код ошибки: %d\n", pids[i], errno);
                }
                else
                {
                    numOfChildProcesses--;
                    printf("Дочерний процесс pid = %d успешно удален\n", pids[i]);
                }
            }
            if (option == 'q') 
            {
                printf("Завершение родительского процесса\n");
                exit(0);
            }
        }
        else
            printf("Введена неверная опция\n");
        
    }
    return 0;
}
