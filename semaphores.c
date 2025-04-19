#include "semaphores.h"

void increaseSemaphore(int semId) {
    struct sembuf sop;
    sop.sem_num = 0; //номер семафора
    sop.sem_op = 1; //увеличить значение на 1
    sop.sem_flg = 0;
    if(semop(semId, &sop, 1) == -1) {
        printf("Ошибка увеличения значения семафора: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}

void decreaseSemaphore(int semId) {
    //printf("\ninside decrease pid=%d\n", getpid());
    struct sembuf sop;
    sop.sem_num = 0; //номер семафора
    sop.sem_op = -1; //уменьшить значение на 1
    sop.sem_flg = 0;
    //printf("\n before semop pid = %d\n", getpid());
    if(semop(semId, &sop, 1) == -1) {
        printf("Ошибка уменьшения значения семафора: %d\n", errno);
        exit(EXIT_FAILURE);
    }
    //printf("\nafter semop=%d\n", getpid());
}

int getValueOfSemaphore(int semId) {
    int rtrn = semctl (semId, 0, GETVAL, 0);
    if(rtrn == -1) {
        printf("Ошибка получения значения семафора: %d\n", errno);
        exit(EXIT_FAILURE);
    }
    return rtrn;
}