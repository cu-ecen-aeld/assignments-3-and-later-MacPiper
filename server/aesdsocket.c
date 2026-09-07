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


#define PORT "9000"
#define CONNECTION_QUEUE_LEN 100
#define RECEIVE_BUFFER_LEN 100

struct addrinfo *servinfo;
char *msgBuffer;
size_t msgBufferLen, msgBufferPos;
ssize_t noOfBytesReceived;
int fileFd, socketFd;

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
        syslog(LOG_ERR, "Problem with getaddrinfo\n%s\n", gai_strerror(result));
        return -1;
    }

    result = bind(socketFd, servinfo->ai_addr, servinfo->ai_addrlen);
    if (result == -1) {
        syslog(LOG_ERR, "Problem with bind: %s\n", strerror(errno));
        return -1;
    }

    result = listen(socketFd, CONNECTION_QUEUE_LEN);
    if (result == -1) {
        syslog(LOG_ERR, "Problem with listen: %s\n", strerror(errno));
        return -1;
    }

    return socketFd;
}

/**
 * Handle termination signals and close the server resources.
 *
 * @param signalNumber Signal number received by the process.
 */
void signalHandler(int signalNumber) {
    // not free'ing memory allocated with malloc as it is not signal Safe
    // not necessary anyway as it is done by OS on exit
    syslog(LOG_DEBUG, "Caught signal, exiting");
    shutdown(socketFd, SHUT_WR);
    close(socketFd);
    close(fileFd);
    _exit(0);
}

/**
 * Register handlers for SIGTERM and SIGINT.
 */
void registerSignalHandlers() {
    struct sigaction new_action;
    memset(&new_action,0,sizeof(struct sigaction));
    new_action.sa_handler = signalHandler;
    if( sigaction(SIGTERM, &new_action, NULL) ) {
        syslog(LOG_ERR, "Problem with registering SIGTERM handler: %s\n", strerror(errno));
    }

    if( sigaction(SIGINT, &new_action, NULL) ) {
        syslog(LOG_ERR, "Problem with registering SIGINT handler: %s\n", strerror(errno));
    }
}


int main(int argc, char *argv[]) {
    int connFd;
    int runAsDaemon = 0;
    pid_t daemonPid;
    struct sockaddr_storage connAddr;
    socklen_t addr_size;
    char recBuf[RECEIVE_BUFFER_LEN];
    int result;
    bool discardMessage = false;

    if (argc > 2 || (argc == 2 && strcmp(argv[1], "-d") != 0)) {
        syslog(LOG_ERR, "Incorrect parameter usage: %s [-d]\n", argv[0]);
        return -1;
    }
    runAsDaemon = argc == 2;

    openlog(NULL, 0, LOG_USER);
    registerSignalHandlers();

    fileFd = open("/var/tmp/aesdsocketdata", O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );
    if (fileFd == -1) {
        syslog(LOG_ERR, "Problem with open file: %s\n", strerror(errno));
        return -1;
    }    

    
    socketFd = createSocket();
    if (socketFd == -1) return -1;

    if (runAsDaemon) {
        daemonPid = fork();
        if (daemonPid == -1) {
            syslog(LOG_ERR, "Problem with fork: %s\n", strerror(errno));
            return -1;
        }
        if (daemonPid > 0) {
            return 0;
        }
    }


    syslog(LOG_DEBUG, "Start accepting connections");
    addr_size = sizeof(connAddr);
    while(1) {
        connFd = accept(socketFd, (struct sockaddr *)&connAddr, &addr_size);
        if (connFd == -1) {
            syslog(LOG_ERR, "Problem with accept: %s\n", strerror(errno));
            return -1;
        }
        // Accepted connection, log the IP address of the client
        char ipAddr[INET6_ADDRSTRLEN];
        if (getPrintableAddressFromSockaddrStorage(&connAddr, ipAddr, sizeof(ipAddr))) {
            syslog(LOG_DEBUG, "Accepted connection from %s",ipAddr);
        } else {
            syslog(LOG_ERR, "Problem with extracting IP Adress: %s\n", strerror(errno));
            return -1;
        }

        // Receive data until newline is received or the connection is closed
        msgBufferLen = RECEIVE_BUFFER_LEN;
        msgBuffer = (char *)realloc((void *)msgBuffer, RECEIVE_BUFFER_LEN);
        msgBufferPos = 0;
        while((noOfBytesReceived = recv(connFd, recBuf, RECEIVE_BUFFER_LEN, 0))) {
            if (noOfBytesReceived == -1) {
                syslog(LOG_ERR, "Problem with recv: %s\n", strerror(errno));
                return -1;
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
                    syslog(LOG_DEBUG, "Message with length %d completed", (int)strlen(msgBuffer));
                    msgBufferPos = 0; // no need to shrink buffer
                    // Write message
                    result = write(fileFd, msgBuffer, strlen(msgBuffer));
                    if (result == -1) {
                        syslog(LOG_ERR, "Problem with writing to file\n%s\n", strerror(errno));
                        return -1;
                    }
                    // Read whole file contents and transmit
                    lseek(fileFd, 0, SEEK_SET);
                    while((result = read(fileFd, msgBuffer, msgBufferLen)) > 0) {
                        ssize_t bytesToSend = result;
                        ssize_t bytesSent = 0;
                        while (bytesSent < bytesToSend) {
                            ssize_t sent = send(connFd,
                                                msgBuffer + bytesSent,
                                                bytesToSend - bytesSent,
                                                0);

                            if (sent == -1) {
                                syslog(LOG_ERR, "Problem with send: %s", strerror(errno));
                                return -1;
                            }

                            bytesSent += sent;
                        }
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
        discardMessage = false;
        close(connFd);
    }
}