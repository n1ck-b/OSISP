#include "header.h"

typedef void (*sighandler_t)(int);
//sighandler_t sa_handler;

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
    //printf("Обработчик сигнала\n");
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
    //printf("\nобработчик usr1 pid = %d\n", getpid());
    raise(SIGTERM);
}

int main(int argc, char* argv[]) 
{
    /*timer_t timerId;
    struct sigevent sev;
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGALRM;
    timer_create(CLOCK_REALTIME, &sev, &timerId);
    struct timespec time;
    time.tv_nsec = 50000;
    time.tv_sec = 0;
    struct itimerspec its;
    its.it_value = time;
    //its.it_interval = time;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;*/
    struct twoNumbers zeros = {0, 0};
    struct twoNumbers ones = {1, 1};
    //обработка сигнала SIGALRM
    struct sigaction newActionForAlarm;
    newActionForAlarm.sa_handler = alarmHandler;
    sigemptyset(&newActionForAlarm.sa_mask);
    sigaddset(&newActionForAlarm.sa_mask, SIGALRM);
    newActionForAlarm.sa_flags= 0;
    sigaction(SIGALRM, &newActionForAlarm, NULL);
    //обработка сигнала SIGUSR1
    struct sigaction newActionForSigUsr1;
    newActionForSigUsr1.sa_handler = sigUsr1Handler;
    sigemptyset(&newActionForSigUsr1.sa_mask);
    newActionForSigUsr1.sa_flags= 0;
    sigaction(SIGUSR1, &newActionForSigUsr1, NULL);
    /*for (int i = 0; i < 500; ++i) 
    {
        //printf("Цикл for %d\n", i);
        if(nanosleep(&time, NULL) == 0)
        {
            raise(SIGALRM);
        }
        ualarm(100, 0);
        //timer_settime(timerId, 0, &its, NULL);
        signalWasReceived = 0;
        while (1) 
        {
            numbers = zeros;
            numbers = ones;
            if (signalWasReceived == 1)
            {
                break;
            }   
        }
    }*/
    int counter = 0;
    while (1) 
    {
        if (counter < 500) 
        {
            ualarm(100, 0);
            signalWasReceived = 0;
            counter++;
            while (1) 
            {
                numbers = zeros;
                numbers = ones;
                if (signalWasReceived == 1)
                {
                    break;
                }   
            }
        }
        if (counter == 500) 
        {
            printf("ppid = %d, pid = %d, {0, 0} = %d, {0, 1} = %d, {1, 0} = %d, {1, 1} = %d\n", getppid(), getpid(), stats.twoZeros, stats.zeroAndOne, stats.oneAndZero, stats.twoOnes);
            counter++;
            kill(getppid(), SIGUSR2);
        }
    }
    return 0;
}

