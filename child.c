#include "header.h"

typedef void (*sighandler_t)(int);

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

int main(int argc, char* argv[]) 
{
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

