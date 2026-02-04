#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <signal.h>

#define PORT "9000"
#define BACKLOG 5
#define SOCKFILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

char *readbuf, *writebuf;
int sockfd, newfd, filefd;
bool terminate = false;

static void cleanup() {
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    shutdown(newfd, SHUT_RDWR);
    close(newfd);
    free(readbuf);
    free(writebuf);
    close(filefd);
    unlink(SOCKFILE);
}

void signal_handler(int sig_num) {
    if (sig_num == SIGINT || sig_num == SIGTERM) {
        terminate = true;
    }
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char* argv[]) {
    openlog(NULL, 0, LOG_USER);

    int status;
    bool packet_received;
    socklen_t sin_size;
    ssize_t recv_bytes, written_bytes, read_bytes, send_bytes;
    struct sockaddr_storage their_addr;
    struct addrinfo *servinfo, hints;
    struct sigaction new_action;
    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    bool bind_success = true;
    if ((status = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        syslog(LOG_ERR, "Error getting address info: %s", gai_strerror(status));
        bind_success = false;
    }

    // TODO: Iterate over addrinfo linked list to find first valid address 
    if ((sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1) {
        syslog(LOG_ERR, "Error creating socket: %s", strerror(errno));
        bind_success = false;
    }

    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        bind_success = false;
        syslog(LOG_ERR, "setsockopt error: %s", strerror(errno));
    }

    if (bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) != 0) {
        syslog(LOG_ERR, "Bind error: %s", strerror(errno));
        bind_success = false;
    }

    freeaddrinfo(servinfo);

    if (!bind_success) {
        closelog();
        return -1;
    }

    bool iffork = false, fork_success = true;
    if (argc == 2) {
        if (strncmp(argv[1], "-d", 2) == 0) {
            iffork = true;
        } else {
            syslog(LOG_ERR, "Wrong parameters");
            fork_success = false;
        }
    }

    if (iffork) {
        pid_t pid = fork();
        if (pid == -1) {
            syslog(LOG_ERR, "fork() error: %s", strerror(errno));
            fork_success = false;
        }
        if (pid != 0) {
            syslog(LOG_INFO, "\"-d\" mentioned, exiting from parent");
            return 0;
        }
        // TODO: setsid and redirect stderr to /dev/null
    }

    if (!fork_success) {
        closelog();
        return -1;
    }

    bool success = true;
    if (listen(sockfd, BACKLOG) != 0) {
        syslog(LOG_ERR, "Listen error: %s", strerror(errno));
        success = false;
    }

    memset(&new_action, 0, sizeof(struct sigaction));
	new_action.sa_handler = signal_handler;
	if (sigaction(SIGTERM, &new_action, NULL) != 0) {
		syslog(LOG_ERR, "Error %d (%s) registering for SIGTERM\n", errno, strerror(errno));
        success = false;
	}
	if (sigaction(SIGINT, &new_action, NULL) != 0) {
		syslog(LOG_ERR, "Error %d (%s) registering for SIGINT\n", errno, strerror(errno));
        success = false;
	}

    readbuf = malloc(BUFFER_SIZE * sizeof(char));
    if (readbuf == NULL) {
        syslog(LOG_ERR, "Malloc error for read buffer: %s", strerror(errno));
        success = false;
    }
    writebuf = malloc(BUFFER_SIZE * sizeof(char));
    if (writebuf == NULL) {
        syslog(LOG_ERR, "Malloc error for write buffer: %s", strerror(errno));
        success = false;
    }

    filefd = open(SOCKFILE, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (filefd == -1) {
        syslog(LOG_ERR, "open() error: %s", strerror(errno));
        success = false;
    }

    if (!success) {
        syslog(LOG_ERR, "Error initalizing socket server");
        closelog();
        return -1;
    }

    while (true) {
        if (terminate) {
            syslog(LOG_INFO, "Caught signal, exiting");
            cleanup();
            closelog();
            return 0;
        }
        sin_size = sizeof(their_addr);
        if ((newfd = accept(sockfd, (struct sockaddr*)&their_addr, &sin_size)) == -1) {
            syslog(LOG_ERR, "Accept error: %s", strerror(errno));
            continue;
        }
        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr*)&their_addr), s, sizeof(s));
        syslog(LOG_INFO, "Accepted connection from %s", s);

        packet_received = false;
        while (!packet_received) {
            recv_bytes = recv(newfd, readbuf, BUFFER_SIZE - 1, 0);
            if (recv_bytes == -1) {
                syslog(LOG_ERR, "recv() error: %s", strerror(errno));
                return -1;
            } else if (recv_bytes == 0) {
                syslog(LOG_DEBUG, "No more bytes to read from client");
                break;
            }

            readbuf[recv_bytes] = '\0';
            if (readbuf[recv_bytes - 1] == '\n') {
                packet_received = true;
            }
            written_bytes = write(filefd, readbuf, recv_bytes);
            if (written_bytes == -1) {
                syslog(LOG_ERR, "write() error: %s", strerror(errno));
                return -1;
            } else if (written_bytes < recv_bytes) {
                syslog(LOG_ERR, "Could not write %ld bytes, written bytes: %ld", recv_bytes, written_bytes);
            } else {
                syslog(LOG_INFO, "Written %ld bytes: %s", written_bytes, readbuf);
            }
        }
        int read_pos = 0;
        while (true) {
            read_bytes = pread(filefd, writebuf, BUFFER_SIZE, read_pos); 
            if (read_bytes == -1) {
            
                syslog(LOG_ERR, "read() error: %s", strerror(errno));
                return -1;
            } else if (read_bytes == 0) {
                syslog(LOG_DEBUG, "No more bytes to read from file");
                break;
            }
            read_pos += read_bytes;
            send_bytes = send(newfd, writebuf, read_bytes, 0);
            if (send_bytes == -1) {
                syslog(LOG_ERR, "send() error: %s", strerror(errno));
                return -1;
            } else {
                syslog(LOG_INFO, "Sent %ld bytes to %s", send_bytes, s);
            }
        }
        syslog(LOG_INFO, "Closed connection from %s", s);
        shutdown(newfd, SHUT_RDWR);
        close(newfd);
    }

    cleanup();
    closelog();

    return 0;
}