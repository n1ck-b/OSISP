#include "header.h"

int childProcessIsFinished = 1;

void sigUsr2Handler(int signum)
{
    childProcessIsFinished = 1;
}

int main(int argc, char* argv[])
{
    setlocale(LC_COLLATE, "C");
    char option;
    int numOfChildProcesses = 0;
    int childStatus;
    pid_t* pids = (pid_t*)calloc(1, sizeof(pid_t));
    struct sigaction actionForSigUsr2;
    actionForSigUsr2.sa_handler = sigUsr2Handler;
    sigemptyset(&actionForSigUsr2.sa_mask);
    actionForSigUsr2.sa_flags= 0;
    sigaction(SIGUSR2, &actionForSigUsr2, NULL);
    while (1)
    {
        if (childProcessIsFinished) 
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
                else if (pids[numOfChildProcesses] != 0) //ветка родительского процесса
                {
                    printf("Порожден дочерний процесс: pid = %d. Кол-во дочерних процессов: %d\n", pids[numOfChildProcesses], numOfChildProcesses + 1);
                    numOfChildProcesses++;
                    pids = (pid_t *)realloc(pids, sizeof(pid_t) * (numOfChildProcesses + 1));
                    childProcessIsFinished = 0;
                }
                else if (pids[numOfChildProcesses] == 0) //ветка дочернего процесса
                {
                    //childProcess();
                    execve("./child", NULL, NULL);
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
                    wait(&childStatus);
                    printf("Последний порожденный дочерний процесс удален: pid = %d, статус: %d\n", pids[numOfChildProcesses - 1], childStatus);
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
                        wait(&childStatus);
                        numOfChildProcesses--;
                        printf("Дочерний процесс успешно удален: pid = %d, статус: %d\n", pids[i], childStatus);
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
        
    }
    return 0;
}
