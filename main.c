#include "header.h"
#include "messagesQueue.h"
#include "producer.h"
#include "consumer.h"
#include "semaphores.h"

#define ADD_SEM_KEY 1
#define FREE_SEM_KEY 2
#define MUTEX_SHM_KEY 3
#define QUEUE_SHM_KEY 4

pid_t* producers;
pid_t* consumers;
MessagesQueue* queue;
int numOfProducers;
int numOfConsumers;
pthread_mutex_t *mutex;

int initialiseMutex() {
    //создание сегмента разделяемой памяти
    int shmId = shmget(MUTEX_SHM_KEY, sizeof(pthread_mutex_t), IPC_CREAT | 0666);
    if (shmId == -1) {
        printf("Ошибка выделения памяти под mutex\n");
        exit(EXIT_FAILURE);
    }
    //присоединение общего сегмента памяти
    mutex = (pthread_mutex_t*)shmat(shmId, NULL, 0);
    if (mutex == (pthread_mutex_t*)-1) {
        printf("Ошибка присоединения памяти\n");
        exit(EXIT_FAILURE);
    }
    //атрибуты мьютекса
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    //предоставляем доступ к мьютексу всем процессам
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    //создаем мьютекс
    pthread_mutex_init(mutex, &attr);
    return shmId;
}

int initialiseSharedMemoryForQueue() {
    //создание сегмента разделяемой памяти
    int shmId = shmget(QUEUE_SHM_KEY, sizeof(MessagesQueue), IPC_CREAT | 0666);
    if (shmId == -1) {
        printf("Ошибка выделения памяти под очередь сообщений\n");
        exit(EXIT_FAILURE);
    }
    //присоединение общего сегмента памяти
    queue = (MessagesQueue*)shmat(shmId, NULL, 0);
    if (queue == (MessagesQueue *)-1) {
        printf("Ошибка присоединения памяти\n");
        exit(EXIT_FAILURE);
    }
    return shmId;
}

void sendSignals(int signal) {
    //сигналы для всех процессов-производителей
    for (int i = 0; i < numOfProducers; ++i) {
        kill(producers[i], signal);
        wait(NULL);
    }
    //сигналы для всех процессов-потребителей
    for (int i = 0; i < numOfConsumers; ++i) {
        kill(consumers[i], signal);
        wait(NULL);
    }
}

int main(int argc, char* argv[]) {
    char option;
    numOfProducers = 0;
    numOfConsumers = 0;
    int shmIdOfMutex = initialiseMutex();
    int shmIdOfQueue = initialiseSharedMemoryForQueue();
    producers = (pid_t*)calloc(1, sizeof(pid_t));
    consumers = (pid_t*)calloc(1, sizeof(pid_t));
    int addSemId = semget(ADD_SEM_KEY, 1, IPC_CREAT | 0666);
    if(addSemId == -1) {
        printf("Ошибка создания семафора для добавленных сообщений: %d\n", errno);
        exit(EXIT_FAILURE);
    }
    int freeSemId = semget(FREE_SEM_KEY, 1, IPC_CREAT | 0666);
    if(freeSemId == -1) {
        printf("Ошибка создания семафора для свободного места: %d\n", errno);
        exit(EXIT_FAILURE);
    }
    initQueue(queue, freeSemId, addSemId);
    while(1) {
        printf("Выберите одну из опций:\n'P' - породить процесс-производитель\n'C' - породить процесс-потребитель\n'p' - удалить процесс-производитель\n'c' - удалить процесс-потребитель\n'i' - просмотреть информацию о текущем состоянии\n'e' - завершение программы\n");
        scanf(" %c", &option);
        while (getchar() != '\n') {};
        switch(option) {
            case 'P':
                createNewProducer();
                break;
            case 'C':
                createNewConsumer();
                break;
            case 'p':
                deleteProducer();
                break;
            case 'c':
                deleteConsumer();
                break;
            case 'i':
                printf("\nРазмер очереди = %d, добавлено сообщений = %d, свободно места = %d, количество производителей = %d, количество потребителей = %d\n", queue->size,
                       getValueOfSemaphore(queue->addedMessages), getValueOfSemaphore(queue->freeSpace), numOfProducers, numOfConsumers);
                sleep(3);
                break;
            case 'e':
                //завершение всех процессов-производителей
                for (int i = 0; i < numOfProducers; ++i) {
                    kill(producers[i], SIGTERM);
                    wait(NULL);
                }
                //завершение всех процессов-потребителей
                for (int i = 0; i < numOfConsumers; ++i) {
                    kill(consumers[i], SIGTERM);
                    wait(NULL);
                }
                //удаление семафора добавленных сообщений
                if (semctl(queue->addedMessages, 0, IPC_RMID) == -1) {
                    perror("Ошибка удаления семафора");
                    exit(EXIT_FAILURE);
                }
                //удаление семафора свободного места
                if (semctl(queue->freeSpace, 0, IPC_RMID) == -1) {
                    perror("Ошибка удаления семафора");
                    exit(EXIT_FAILURE);
                }
                shmdt(mutex);  //отключение общего сегмента памяти мьютекса
                shmctl(shmIdOfMutex, IPC_RMID, NULL); //удаление общего сегмента памяти мьютекса
                clearQueue(queue);
                shmdt(queue); //отключение общего сегмента памяти очереди сообщений
                shmctl(shmIdOfQueue, IPC_RMID, NULL); //удаление общего сегмента памяти очереди сообщений
                free(producers);
                free(consumers);
                return 0;
            default:
                printf("Введена неверная опция\n");
                break;
        }
    }
}
