#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include "queue.h"


#define PORT "9000"
#define CONNECTION_QUEUE_LEN 100
#define RECEIVE_BUFFER_LEN 100
#define TIMESTAMP_LEN 100

/* TODOs 
    x Locking for file access
    Clean return from the connection thread
    x Signal handler
    x Thread for timestamps
    Supplement missing failure handlings
*/

// File descriptor for the network socket to listen to
int socketFd;

// File descriptor for the temp file including mutex for safe access
struct fileDescriptor_s {
    int fd;
    pthread_mutex_t mutex;
};
typedef struct fileDescriptor_s fileDescriptor_t;
fileDescriptor_t tmpFile;

// Declare a list of a structure which holds connection information for each thread
struct connectionList_s {
    pthread_t threadId;
    int connFd;
    atomic_bool connectionClosed;
    struct sockaddr_storage connAddr;
    SLIST_ENTRY(connectionList_s) entries;
};
typedef struct connectionList_s connectionList_t;
SLIST_HEAD(head, connectionList_s) connectionListHead;
connectionList_t *nextConnectionList_p;

volatile sig_atomic_t signalForExitingReceived_b = false;
atomic_bool stopTimerThread_b = false;


/**
 * Convert an IPv4 or IPv6 socket address to printable text.
 *
 * The caller owns output and must provide enough space for the address and
 * its terminating null byte: INET_ADDRSTRLEN for IPv4 or INET6_ADDRSTRLEN
 * for IPv6. The output buffer is left unchanged for an unsupported address
 * family. The returned pointer is output on success, or NULL on failure.
 *
 * @param address_sasp Socket address to convert.
 * @param output Caller-provided buffer for the printable address.
 * @param output_len Size of output in bytes.
 * @return output on success, or NULL if the address family is unsupported
 *         or inet_ntop() fails.
 */
const char *getPrintableAddressFromSockaddrStorage(const struct sockaddr_storage *address_sasp,
    char *output,
    size_t output_len) {
    if(address_sasp->ss_family == AF_INET) {
        struct sockaddr_in *sap = (struct sockaddr_in *)address_sasp;
        return inet_ntop(AF_INET, &(sap->sin_addr), output, output_len);
    } else if(address_sasp->ss_family == AF_INET6) {
        struct sockaddr_in6 *sa6p = (struct sockaddr_in6 *)address_sasp;
        return inet_ntop(AF_INET6, &(sa6p->sin6_addr), output, output_len);
    } else {
        return NULL;
    }
}

/**
 * Create, bind, and listen on the server socket.
 *
 * The socket listens on PORT for IPv6 connections and returns the listening
 * socket file descriptor on success. On failure, -1 is returned.
 *
 * @return listening socket file descriptor on success, or -1 on failure.
 */
int createSocket() {
    struct addrinfo hints;
    struct addrinfo *servinfo;
    int socketFd, result;   

    memset(&hints, 0,  sizeof(struct addrinfo));
    hints.ai_family = AF_INET6; // According to man page, this will allow both IPv4 and IPv6 connections
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    socketFd = socket(hints.ai_family, hints.ai_socktype, 0);
    if (socketFd == -1) {
        syslog(LOG_ERR, "Problem with socket: %s\n", strerror(errno));
        return -1;
    }

    result = getaddrinfo(NULL, PORT, &hints, &servinfo);
    if (result) {
        close(socketFd);
        syslog(LOG_ERR, "Problem with getaddrinfo\n%s\n", gai_strerror(result));
        return -1;
    }

    result = bind(socketFd, servinfo->ai_addr, servinfo->ai_addrlen);
    if (result == -1) {
        freeaddrinfo(servinfo);
        close(socketFd);
        syslog(LOG_ERR, "Problem with bind: %s\n", strerror(errno));
        return -1;
    }
    freeaddrinfo(servinfo);

    result = listen(socketFd, CONNECTION_QUEUE_LEN);
    if (result == -1) {
        close(socketFd);
        syslog(LOG_ERR, "Problem with listen: %s\n", strerror(errno));
        return -1;
    }

    return socketFd;
}

/**
 * Handle termination signals by setting a flag to indicate that the server should exit
 *
 * @param signalNumber Signal number received by the process.
 */
void signalHandler(int signalNumber) {
    (void)signalNumber; // Unused parameter
    signalForExitingReceived_b = true;
}

/**
 * Register handlers for SIGTERM and SIGINT.
 */
void registerSignalHandlers() {
    struct sigaction new_action;
    memset(&new_action,0,sizeof(struct sigaction));
    new_action.sa_handler = signalHandler;
    sigemptyset(&new_action.sa_mask);
    if( sigaction(SIGTERM, &new_action, NULL) ) {
        syslog(LOG_ERR, "Problem with registering SIGTERM handler: %s\n", strerror(errno));
    }

    if( sigaction(SIGINT, &new_action, NULL) ) {
        syslog(LOG_ERR, "Problem with registering SIGINT handler: %s\n", strerror(errno));
    }
}

static void connectionThreadCleanup(connectionList_t *parameters, char *msgBuffer) {
    close(parameters->connFd);
    free(msgBuffer);
    atomic_store(&parameters->connectionClosed, true);
}

/**
 * Handles a single client connection in a separate thread
 */
static void *connectionThread(void *arg) {
    char *msgBuffer = NULL;
    size_t msgBufferLen, msgBufferPos;
    ssize_t noOfBytesReceived;
    char recBuf[RECEIVE_BUFFER_LEN];
    bool discardMessage = false;
    int result;

    connectionList_t *parameters = (connectionList_t *)arg;
    int connFd = parameters->connFd;
    
    char ipAddr[INET6_ADDRSTRLEN];
    if (getPrintableAddressFromSockaddrStorage(&parameters->connAddr, ipAddr, sizeof(ipAddr))) {
        syslog(LOG_DEBUG, "Accepted connection from %s",ipAddr);
    } else {
        syslog(LOG_ERR, "Problem with extracting IP Adress: %s\n", strerror(errno));
        connectionThreadCleanup(parameters, msgBuffer);
        return arg;
    }

    // Receive data until newline is received or the connection is closed

    void *temp = (char *)malloc(RECEIVE_BUFFER_LEN);
    if (temp != NULL) {
        msgBuffer = temp;
        msgBufferLen = RECEIVE_BUFFER_LEN;
    } else {
        syslog(LOG_ERR, "Problem with allocating memory for message buffer: %s", strerror(errno));
        connectionThreadCleanup(parameters, msgBuffer);
        return arg;
    }    
    msgBufferPos = 0;
    while((noOfBytesReceived = recv(connFd, recBuf, RECEIVE_BUFFER_LEN, 0))) {
        if (noOfBytesReceived == -1) {
            syslog(LOG_ERR, "Problem with recv: %s\n", strerror(errno));
            connectionThreadCleanup(parameters, msgBuffer);
            return arg;
        }
        if (!discardMessage) {
            if (msgBufferLen < msgBufferPos + noOfBytesReceived + 1) {
                size_t newBufferLen = msgBufferLen + RECEIVE_BUFFER_LEN;
                void *temp = realloc(msgBuffer, newBufferLen);

                if (temp != NULL) {
                    msgBuffer = temp;
                    msgBufferLen = newBufferLen;
                } else {
                    syslog(LOG_ERR, "Problem with realloc: %s", strerror(errno));
                    discardMessage = true;
                }
            }
        }
        if (!discardMessage) {
            strncpy(&msgBuffer[msgBufferPos], recBuf, noOfBytesReceived);
            msgBufferPos += noOfBytesReceived;
            msgBuffer[msgBufferPos] = '\0';
            if (strrchr(msgBuffer,'\n')) {
                msgBufferPos = 0; // no need to shrink buffer
                // Write message
                result = pthread_mutex_lock(&tmpFile.mutex);
                if (result != 0) {
                    syslog(LOG_ERR, "Problem with aquiring mutex to write to file to file\n%s\n", strerror(result));
                    connectionThreadCleanup(parameters, msgBuffer);
                    return arg;
                }
                // Hanlde write same way as send() in connectionThread to ensure all bytes are written
                ssize_t bytesToWrite = strlen(msgBuffer);
                ssize_t bytesWritten = 0;
                while (bytesWritten < bytesToWrite) {
                    ssize_t written = write(tmpFile.fd, msgBuffer + bytesWritten, bytesToWrite - bytesWritten);
                    if (written == -1) {
                        syslog(LOG_ERR, "Problem with writing to file: %s", strerror(errno));
                        pthread_mutex_unlock(&tmpFile.mutex);
                        connectionThreadCleanup(parameters, msgBuffer);
                        return arg;
                    }
                    bytesWritten += written;
                }
                // Read whole file contents and transmit
                result = lseek(tmpFile.fd, 0, SEEK_SET);
                if (result == -1) {
                    syslog(LOG_ERR, "Problem with seeking to beginning of file: %s", strerror(errno));
                    pthread_mutex_unlock(&tmpFile.mutex);
                    connectionThreadCleanup(parameters, msgBuffer);
                    return arg;
                }
                while((result = read(tmpFile.fd, msgBuffer, msgBufferLen)) > 0) {
                    ssize_t bytesToSend = result;
                    ssize_t bytesSent = 0;
                    while (bytesSent < bytesToSend) {
                        ssize_t sent = send(connFd,
                                            msgBuffer + bytesSent,
                                            bytesToSend - bytesSent,
                                            MSG_NOSIGNAL);

                        if (sent == -1) {
                            syslog(LOG_ERR, "Problem with send: %s", strerror(errno));
                            pthread_mutex_unlock(&tmpFile.mutex);
                            connectionThreadCleanup(parameters, msgBuffer);
                            return arg;
                        }

                        bytesSent += sent;
                    }
                }
                if (result == -1) {
                    syslog(LOG_ERR, "Problem with reading from file: %s", strerror(errno));
                    pthread_mutex_unlock(&tmpFile.mutex);
                    connectionThreadCleanup(parameters, msgBuffer);
                    return arg;
                }
                result = pthread_mutex_unlock(&tmpFile.mutex);
                if (result != 0) {
                    syslog(LOG_ERR, "Problem with releasing mutex for tmp-file access: %s", strerror(result));
                    connectionThreadCleanup(parameters, msgBuffer);
                    return arg;
                }

            }
        } else { // discardMessage==true
            if (memchr(recBuf,'\n',(size_t)noOfBytesReceived)) {
                syslog(LOG_DEBUG, "Message completed but discarded due to size");
                msgBufferPos = 0; // no need to shrink buffer
                discardMessage = false;
            }
        }

    }

    syslog(LOG_DEBUG, "Closed connection from %s", ipAddr);
    connectionThreadCleanup(parameters, msgBuffer);
    return arg;
}

/**
 * Handles writing timestamp to tmp file in a separate thread
 */
static void timestampThread(union sigval sigval ) {
    (void)sigval; // Unused parameter
    int result;

    time_t t = time(NULL);
    struct tm *tmp = localtime(&t);
    if (tmp == NULL) {
        syslog(LOG_ERR, "Problem with getting localtime: %s", strerror(errno));
        return;
    }

    char timeStamp[TIMESTAMP_LEN] = "timestamp: ";

    result = strftime(timeStamp + strlen(timeStamp), sizeof(timeStamp) - strlen(timeStamp), "%y/%m/%d %T", tmp);
    if (result == 0) {
        syslog(LOG_ERR, "Problem with formatting timestamp: %s", strerror(errno));
        return;
    }
    if (strcat(timeStamp, "\n") == NULL) {
        syslog(LOG_ERR, "Problem with formatting timestamp: %s", strerror(errno));
        return;
    }
    result = pthread_mutex_lock(&tmpFile.mutex);
    if (result != 0) {
        syslog(LOG_ERR, "Problem with aquiring mutex to write to file to file\n%s\n", strerror(result));
        return;
    }
    if (atomic_load(&stopTimerThread_b)) { // write to file only if exit is not yet flagged
        pthread_mutex_unlock(&tmpFile.mutex);
        return;
    }    
    ssize_t bytesToWrite = strlen(timeStamp);
    ssize_t bytesWritten = 0;
    while (bytesWritten < bytesToWrite) {
        ssize_t written = write(tmpFile.fd, timeStamp + bytesWritten, bytesToWrite - bytesWritten);
        if (written == -1) {
            syslog(LOG_ERR, "Problem with writing to file: %s", strerror(errno));
            pthread_mutex_unlock(&tmpFile.mutex);
            return;
        }
        bytesWritten += written;
    }
    result = pthread_mutex_unlock(&tmpFile.mutex);
    if (result != 0) {
        syslog(LOG_ERR, "Problem with releasing mutex for tmp-file access: %s", strerror(result));
        return;
    }
    return;
}

/**
 * Registers the timer which cyclically triggers the timestampThread
 */
int registerTimer(timer_t *timerId) {
    struct sigevent sev;
    int clockId = CLOCK_MONOTONIC;    
    struct itimerspec  its;
    int result;

    memset(&sev,0,sizeof(struct sigevent));
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = timestampThread;
    sev.sigev_notify_attributes = NULL;
    result = timer_create(clockId,&sev,timerId);
    if (result != 0) {
        syslog(LOG_ERR, "Problem with creating timer: %s", strerror(errno));
        return -1;
    }
    its.it_value.tv_sec = 10;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;

    result = timer_settime(*timerId, 0, &its, NULL);
    if (result == -1) {
        syslog(LOG_ERR, "Problem with setting timer: %s", strerror(errno));
        timer_delete(*timerId);
        return -1;
    }

    return 0;
}


int main(int argc, char *argv[]) {
    int connFd;
    int runAsDaemon = 0;
    pid_t daemonPid;
    struct sockaddr_storage connAddr;
    socklen_t addr_size;
    SLIST_INIT(&connectionListHead);
    connectionList_t *connectionList_p;
    timer_t timerId;
    int result;

    if (argc > 2 || (argc == 2 && strcmp(argv[1], "-d") != 0)) {
        syslog(LOG_ERR, "Incorrect parameter usage: %s [-d]\n", argv[0]);
        return EXIT_FAILURE;
    }
    runAsDaemon = argc == 2;

    openlog(NULL, 0, LOG_USER);
    registerSignalHandlers();

    tmpFile.fd = open("/var/tmp/aesdsocketdata", O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );
    if (tmpFile.fd == -1) {
        syslog(LOG_ERR, "Problem with open file: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    result = pthread_mutex_init(&tmpFile.mutex, NULL);
    if (result != 0) {
        syslog(LOG_ERR, "Problem with initialization of mutex for file access: %s\n", strerror(result));
        return EXIT_FAILURE;
    }
    
    socketFd = createSocket();
    if (socketFd == -1) return EXIT_FAILURE;

    if (runAsDaemon) {
        daemonPid = fork();
        if (daemonPid == -1) {
            syslog(LOG_ERR, "Problem with fork: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }
        if (daemonPid > 0) {
            return 0;
        }
    }
    // Timer registration is done after the fork to avoid the timer thread being created in the parent process
    if (registerTimer(&timerId) != 0) {
        syslog(LOG_ERR, "Timer registration failed");
        return EXIT_FAILURE;
    }

    syslog(LOG_DEBUG, "Start accepting connections");
    while(!signalForExitingReceived_b) {
        addr_size = sizeof(connAddr);
        connFd = accept(socketFd, (struct sockaddr *)&connAddr, &addr_size);
        if (connFd == -1) {
            if (errno == EINTR && signalForExitingReceived_b) {
                break;
            }
            syslog(LOG_ERR, "Problem with accept: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }

        connectionList_p = (connectionList_t *)malloc(sizeof(connectionList_t));
        if (connectionList_p == NULL) {
            syslog(LOG_ERR, "Problem with allocatin memory for connectionList entry: %s\n", strerror(errno));
            close(connFd);
            return EXIT_FAILURE;
        }        
        atomic_init(&connectionList_p->connectionClosed, false);
        connectionList_p->connFd = connFd;
        connectionList_p->connAddr = connAddr;
        result =  pthread_create(&connectionList_p->threadId, NULL, connectionThread, connectionList_p);
        if (result != 0) {
            syslog(LOG_ERR, "Problem with creating thread: %s\n", strerror(result));
            close(connFd);
            free(connectionList_p);
            return EXIT_FAILURE;
        }
        SLIST_INSERT_HEAD(&connectionListHead, connectionList_p, entries);
        SLIST_FOREACH_SAFE(connectionList_p, &connectionListHead, entries, nextConnectionList_p) {
            if (atomic_load(&connectionList_p->connectionClosed)) {
                pthread_join(connectionList_p->threadId, NULL);
                // Remove entry from list and free memory
                SLIST_REMOVE(&connectionListHead, connectionList_p, connectionList_s, entries);
                free(connectionList_p);
            }
        }
    }

    syslog(LOG_DEBUG, "Caught signal, exiting");
    SLIST_FOREACH_SAFE(connectionList_p, &connectionListHead, entries, nextConnectionList_p) {
        shutdown(connectionList_p->connFd, SHUT_RDWR);
        pthread_join(connectionList_p->threadId, NULL);
        // Remove entry from list and free memory
        SLIST_REMOVE(&connectionListHead, connectionList_p, connectionList_s, entries);
        free(connectionList_p);
    }
    // This is reached only when a signal is received, safely close the socket and delete the timer
    shutdown(socketFd, SHUT_WR);
    close(socketFd);
    // Delete timer and make sure the timer thread is no more using the file descriptor before closing it
    atomic_store(&stopTimerThread_b, true);
    result = timer_delete(timerId);
    if (result == -1) {
        syslog(LOG_ERR, "Problem with deleting timer: %s", strerror(errno));
    }
    result = pthread_mutex_lock(&tmpFile.mutex);
    if (result != 0) {
        syslog(LOG_ERR, "Problem with acquiring mutex during shutdown: %s", strerror(result));
    } else {
        pthread_mutex_unlock(&tmpFile.mutex);
    }    
    close(tmpFile.fd);

    return 0;
}