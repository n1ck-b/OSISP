#include "server.h"
#include "libs.h"

int portNum;
int serverSocket;
volatile int numOfClients;
char* rootDir;
ClientConnection* clients;
pthread_mutex_t* mutex;

void initialiseMutex() {
    mutex = (pthread_mutex_t*)calloc(1, sizeof(pthread_mutex_t));
    //создаем мьютекс
    int status = pthread_mutex_init(mutex, NULL);
    if (status != 0) {
        fprintf(stderr, "Ошибка создания мьютекса: %s\n", strerror(status));
        exit(status);
    }
}

void createSocket() {
    //создаем сокет с семейством протоколов AF_INET(IPv4), типа STREAM, протокол TCP
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        fprintf(stderr, "Ошибка создания сокета для сервера: %s\n", strerror(errno));
        exit(errno);
    }
}

void bindSocket() {
    struct sockaddr_in address;
    address.sin_family = AF_INET; //семейство протоколов
    address.sin_port = htons(portNum); //переводим номер порта в сетевой порядок байт
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); //адрес (localhost) в сетевом порядке байт
    //присваиваем адрес сокету
    if (bind(serverSocket, (struct sockaddr*)&address, sizeof(struct sockaddr_in)) == -1) {
        fprintf(stderr, "Ошибка присваивания адреса сокету сервера: %s\n", strerror(errno));
        exit(errno);
    }
}

void listenConnection() {
    if (listen(serverSocket, SOMAXCONN) == -1) {
        fprintf(stderr, "Ошибка подготовки сокета сервера к приему входящих соединений: %s\n", strerror(errno));
        exit(errno);
    }
}

int acceptConnection() {
    //ожидаем запроса на подключение
    int clientSocket = accept(serverSocket, NULL, NULL);
    if (clientSocket == -1) {
        fprintf(stderr, "Ошибка установки соединения: %s\n", strerror(errno));
        exit(errno);
    }
    return clientSocket;
}

char* getTimestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    struct tm *tm_info;
    tm_info = localtime(&tv.tv_sec);

    char* time_buffer = (char*)calloc(64, sizeof(char));

    strftime(time_buffer, 32, "%Y.%m.%d-%H:%M:%S", tm_info);
    snprintf(time_buffer + strlen(time_buffer), 32 - strlen(time_buffer), ".%03ld", tv.tv_usec / 1000);

    return time_buffer;
}


void formatString(char* buffer, char* message) {
    char* timestamp = getTimestamp();
    //формируем ответ с временной меткой
    snprintf(buffer, 1024, "%s %s\n", timestamp, message);
    free(timestamp);
}

void getStringWithHintForClient(char* relativePath, char* currentDir) {
    memset(relativePath, 0, sizeof(*relativePath));
    //получаем путь без корневого каталога
    strncpy(relativePath, currentDir + strlen(rootDir) + 1, PATH_MAX - 1);
    strcat(relativePath, "> ");
}

void* handleConnection(void* arg) {
    char buffer[1024];
    ClientConnection connection = clients[numOfClients - 1];
    printf("Соединение с клиентом %d установлено\n", connection.clientSocket);
    //переходим в корневой каталог
    if (chdir(connection.currentDir) != 0) {
        fprintf(stderr, "Ошибка смены каталога: %s\n", strerror(errno));
        close(connection.clientSocket);
        exit(errno);
    }
    //формируем ответ с временной меткой
    formatString(buffer, "Соединение установлено");
    if (send(connection.clientSocket, buffer, strlen(buffer), 0) == -1) {
        fprintf(stderr, "Ошибка отправки сообщения клиенту: %s\n", strerror(errno));
        close(connection.clientSocket);
        exit(errno);
    }
    char relativePath[PATH_MAX];
    while(1) {
        getStringWithHintForClient(relativePath, connection.currentDir);
        //отправляем строку с ">"
        if (send(connection.clientSocket, relativePath, strlen(relativePath), 0) == -1) {
            fprintf(stderr, "Ошибка отправки сообщения клиенту: %s\n", strerror(errno));
            break;
        }
        //очищаем буфер
        memset(buffer, 0, sizeof(buffer));
        int receivedBytes = recv(connection.clientSocket, buffer, 1023, 0);
        if (receivedBytes == -1) {
            fprintf(stderr, "Ошибка чтения сообщения от клиента: %s\n", strerror(errno));
            break;
        }

        if (strncasecmp(buffer, "ECHO", 4) == 0) {
            //формируем ответ с временной меткой
            char echoText[1024];
            strcpy(echoText, buffer + 5);
            formatString(buffer, echoText);
            //отправляем обратно полученное сообщение
            if (send(connection.clientSocket, buffer, strlen(buffer), 0) == -1) {
                fprintf(stderr, "Ошибка отправки сообщения клиенту: %s\n", strerror(errno));
                break;
            }
        }

        //завершаем сеанс
        else if (strncasecmp(buffer, "QUIT", 4) == 0) {
            //формируем ответ с временной меткой
            formatString(buffer, "Отключение соединения");
            if (send(connection.clientSocket, buffer, strlen(buffer), 0) == -1) {
                fprintf(stderr, "Ошибка отправки сообщения клиенту: %s\n", strerror(errno));
            }
            printf("Соединение с клиентом %d разорвано\n", connection.clientSocket);
            break;
        }

        //отправка информации о сервере
        else if (strncasecmp(buffer, "INFO", 4) == 0) {
            FILE *infoFile = fopen(INFO_FILE_PATH, "r");
            //если файл не найден
            if (!infoFile) {
                char errorMessage[200];
                formatString(errorMessage, "Ошибка: файл INFO не найден");
                if (send(connection.clientSocket, errorMessage, strlen(errorMessage), 0) == -1) {
                    fprintf(stderr, "Ошибка отправки сообщения клиенту: %s\n", strerror(errno));
                    fclose(infoFile);
                    break;
                }
            }
            //очищаем буфер
            memset(buffer, 0, sizeof(buffer));
            //читаем и отправляем данные из файла
            while (fgets(buffer, sizeof(buffer), infoFile)) {
                if (send(connection.clientSocket, buffer, strlen(buffer), 0) == -1) {
                    fprintf(stderr, "Ошибка отправки сообщения клиенту: %s\n", strerror(errno));
                    break;
                }
            }
            fclose(infoFile);
        }

        //переход в другой каталог
        else if (strncasecmp(buffer, "CD", 2) == 0) {
            char newPath[PATH_MAX];
            //новый путь относительно текущего каталога
            if (strlen(connection.currentDir) + strlen(buffer + 3) + 2 < PATH_MAX) {
                snprintf(newPath, PATH_MAX + strlen(connection.currentDir) + strlen(buffer + 3) + 2, "%s/%s", connection.currentDir, buffer + 3);
            }
            else {
                char errorMessage[400];
                snprintf(errorMessage, 300, "Путь превышает длину PATH_MAX = %d", PATH_MAX);
                formatString(errorMessage, "");
                if (send(connection.clientSocket, errorMessage, strlen(errorMessage), 0) == -1) {
                    fprintf(stderr, "Ошибка отправки сообщения клиенту: %s\n", strerror(errno));
                    break;
                }
                continue;
            }
            //проверяем конечный абсолютный путь
            char* resolvedPath = realpath(newPath, NULL);
            if (resolvedPath == NULL) {
                //если путь неверный
                char errorMessage[400];
                snprintf(errorMessage, 300, "Неверный путь: %s", strerror(errno));
                //добавляем временную метку
                memset(buffer, 0, sizeof(buffer));
                formatString(buffer, errorMessage);
                if (send(connection.clientSocket, buffer, strlen(buffer), 0) == -1) {
                    fprintf(stderr, "Ошибка отправки сообщения клиенту: %s\n", strerror(errno));
                    break;
                }
                continue;
            }
            memset(newPath, 0, sizeof(newPath));
            strcpy(newPath, resolvedPath);
            free(resolvedPath);
            //проверяем, начинается ли новый абсолютный путь с корневого каталога, если нет - игнорируем команду
            if (strncmp(newPath, rootDir, strlen(rootDir)) != 0) {
                continue;
            }
            if (chdir(newPath) != 0) {
                fprintf(stderr, "Ошибка смены каталога: %s\n", strerror(errno));
                break;
            }
            memset(connection.currentDir, 0, sizeof(connection.currentDir));
            strcpy(connection.currentDir, newPath);
        }

        else if (strncasecmp(buffer, "LIST", 4) == 0) {
            DIR *dir = opendir(connection.currentDir);
            if (!dir) {
                fprintf(stderr,"Ошибка: не удалось открыть каталог\n");
                continue;
            }
            memset(buffer, 0, sizeof(buffer));
            //отправляем временную метку
            formatString(buffer, "");
            if (send(connection.clientSocket, buffer, strlen(buffer), 0) == -1) {
                fprintf(stderr, "Ошибка отправки сообщения клиенту: %s\n", strerror(errno));
                break;
            }
            struct dirent *entry;
            //пока есть что читать
            while ((entry = readdir(dir)) != NULL) {
                struct stat fileStat;
                //полный путь к файлу/каталогу/ссылку в текущем каталоге
                char fullPath[PATH_MAX + strlen(entry->d_name) + 2];
                snprintf(fullPath, PATH_MAX + strlen(entry->d_name) + 2, "%s/%s", connection.currentDir, entry->d_name);
                //получаем информацию о файле/каталоге/ссылке
                stat(fullPath, &fileStat);
                char messageToSent[PATH_MAX];
                //каталог
                if (S_ISDIR(fileStat.st_mode)) {
                    snprintf(messageToSent, PATH_MAX, "%s/\n", entry->d_name);
                    if (send(connection.clientSocket, messageToSent, strlen(messageToSent), 0) == -1) {
                        fprintf(stderr, "Ошибка отправки сообщения клиенту: %s\n", strerror(errno));
                        break;
                    }
                }
                //символическая ссылка
                else if (S_ISLNK(fileStat.st_mode)) {
                    snprintf(messageToSent, PATH_MAX, "%s --> %s\n", entry->d_name, realpath(fullPath, NULL));
                    if (send(connection.clientSocket, messageToSent, strlen(messageToSent), 0) == -1) {
                        fprintf(stderr, "Ошибка отправки сообщения клиенту: %s\n", strerror(errno));
                        break;
                    }
                }
                //файл
                else {
                    snprintf(messageToSent, PATH_MAX, "%s\n", entry->d_name);
                    if (send(connection.clientSocket, messageToSent, strlen(messageToSent), 0) == -1) {
                        fprintf(stderr, "Ошибка отправки сообщения клиенту: %s\n", strerror(errno));
                        break;
                    }
                }
            }
            closedir(dir);
        }

        else {
            char errorMessage[200];
            formatString(errorMessage, "Неизвестная команда");
            if (send(connection.clientSocket, errorMessage, strlen(errorMessage), 0) == -1) {
                fprintf(stderr, "Ошибка отправки сообщения клиенту: %s\n", strerror(errno));
                break;
            }
        }
    }
    close(connection.clientSocket);
    pthread_mutex_lock(mutex);
    numOfClients--;
    if (numOfClients != 0) {
        clients = (ClientConnection*)realloc(clients, sizeof(ClientConnection) * numOfClients);
    }
    pthread_mutex_unlock(mutex);
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Неверное число аргументов. Формат запуска: server root_dir port_no\n");
        exit(EXIT_FAILURE);
    }
    if (strlen(argv[1]) > PATH_MAX) {
        fprintf(stderr, "Путь к корневому каталогу должен быть не длиннее %d символов\n", PATH_MAX);
        exit(EXIT_FAILURE);
    }
    //корневой каталог
    char* tempRoot = realpath(argv[1], NULL);
    if (tempRoot == NULL) {
        fprintf(stderr, "Ошибка распознавания пути к корневому каталогу: %s\n", strerror(errno));
        exit(errno);
    }
    rootDir = (char*)calloc(PATH_MAX, sizeof(char));
    strcpy(rootDir, tempRoot);
    free(tempRoot);

    portNum = strtol(argv[2], NULL, 10);
    clients = NULL;
    initialiseMutex();
    //создание сокета
    createSocket();
    //присваивание адреса сокету
    bindSocket();
    //подготовка к прослушиванию запросов на подключение
    listenConnection();
    printf("\nСервер ожидает подключений\n");
    //файловый дескриптор для отслеживания
    struct pollfd fds[1];
    fds[0].fd = serverSocket; //файловый дескриптор
    fds[0].events = POLLIN; //ожидаем запроса на подключение
    //пока есть клиенты, сервер работает
    do {
        //проверяем, есть ли входящее подключение
        int ret = poll(fds, 1, 10000);

        //если нет клиентов в течение 10 секунд
        if (ret == 0 && numOfClients == 0) {
            break;
        }
        if (ret < 0) {
            fprintf(stderr, "Ошибка при ожидании клиентов: %s\n", strerror(errno));
            break;
        }
        //если есть запрос на подключение
        if (fds[0].revents & POLLIN) {
            //ожидаем запроса на подключение
            int clientSocket = acceptConnection();
            //когда произошло подключение
            pthread_mutex_lock(mutex);
            numOfClients++;
            clients = (ClientConnection *) realloc(clients, sizeof(ClientConnection) * numOfClients);
            clients[numOfClients - 1].clientSocket = clientSocket;
            strcpy(clients[numOfClients - 1].currentDir, rootDir);
            pthread_mutex_unlock(mutex);
            //создаем новый поток для текущего подключения
            pthread_t thread;
            pthread_create(&thread, NULL, handleConnection, NULL);
            //отсоединяем поток
            pthread_detach(thread);
        }
    } while (numOfClients != 0);
    printf("\nЗавершение работы сервера\n");
    //удаление мьютекса
    int retVal = pthread_mutex_destroy(mutex);
    if (retVal != 0) {
        printf("Ошибка удаления мьютекса: %s\n", strerror(retVal));
        exit(retVal);
    }
    free(rootDir);
    free(clients);
    return 0;
}