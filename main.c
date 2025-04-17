#include "header.h"
#include "messagesQueue.h"
#include "producer.h"
#include "consumer.h"
#include "semaphores.h"

#define ADD_SEM_KEY 1
#define FREE_SEM_KEY 2

pid_t* producers;
pid_t* consumers;
MessagesQueue* queue;
int numOfProducers;
int numOfConsumers;
pthread_mutex_t mutex;

int main(int argc, char* argv[]) {
    char option;
    numOfProducers = 0;
    numOfConsumers = 0;
    pthread_mutex_init(&mutex, NULL);
    producers = (pid_t*)calloc(1, sizeof(pid_t));
    consumers = (pid_t*)calloc(1, sizeof(pid_t));
    queue = (MessagesQueue*)calloc(1, sizeof(MessagesQueue));
    int addSemId = semget(ADD_SEM_KEY, 1, IPC_CREAT | 0666);
    int freeSemId = semget(FREE_SEM_KEY, 1, IPC_CREAT | 0666);
    initQueue(queue, freeSemId, addSemId);
    while(1) {
        printf("Выберите одну из опций:\n'P' - породить процесс-производитель\n'C' - породить процесс-потребитель\n'p' - удалить процесс-производитель\n'c' - удалить процесс-потребитель\n'i' - просмотреть информацию о текущем состоянии\n'e' - завершение программы\n");
//        option = getchar();
        scanf(" %c", &option);
        while (getchar() != '\n') {};
        switch(option) {
            case 'P':
//                if(createNewProducer() != -1) {
//                    numOfProducers++;
//                    producers = (pid_t*)realloc(producers, numOfProducers * sizeof(pid_t));
//                }
                createNewProducer();
                printf("Создан новый процесс-производитель\n");
                break;
            case 'C':
                createNewConsumer();
                printf("Создан новый процесс-потребитель\n");
                break;
            case 'p':
                deleteProducer();
                break;
            case 'c':
                deleteConsumer();
                break;
            case 'i':
                printf("Размер очереди = %d, добавлено сообщений = %d, свободно места = %d, количество производителей = %d, количество потребителей = %d\n", queue->size,
                       getValueOfSemaphore(queue->addedMessages), getValueOfSemaphore(queue->freeSpace), numOfProducers, numOfConsumers);
                break;
            case 'e':
                for (int i = 0; i < numOfProducers; ++i) {
                    kill(producers[i], SIGTERM);
                    wait(NULL);
                }
                for (int i = 0; i < numOfConsumers; ++i) {
                    kill(consumers[i], SIGTERM);
                    wait(NULL);
                }
                if (semctl(queue->addedMessages, 0, IPC_RMID) == -1) {
                    perror("Ошибка удаления семафора");
                    exit(EXIT_FAILURE);
                }
                if (semctl(queue->freeSpace, 0, IPC_RMID) == -1) {
                    perror("Ошибка удаления семафора");
                    exit(EXIT_FAILURE);
                }
                clearQueue(queue);
                free(producers);
                free(consumers);
                return 0;
            default:
                printf("Введена неверная опция\n");
                break;
        }
    }
}
