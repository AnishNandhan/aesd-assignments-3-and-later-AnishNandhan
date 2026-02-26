#define _GNU_SOURCE

#include "aesdsocket.h"
#include "aesd_ioctl.h"
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <time.h>

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#define PORT "9000"
#define BACKLOG 5
#if USE_AESD_CHAR_DEVICE
#define SOCKFILE "/dev/aesdchar"
#else
#define SOCKFILE "/var/tmp/aesdsocketdata"
#endif
#define BUFFER_SIZE 2048

int sockfd, filefd_for_time;
pthread_mutex_t file_mutex;
bool terminate = false;

static void cleanup() {
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
#if !USE_AESD_CHAR_DEVICE
    close(filefd_for_time);
    unlink(SOCKFILE);
#endif
    pthread_mutex_destroy(&file_mutex);
}

static void print_time() {
    int rc;
    time_t t;
    struct tm break_time;
    char outtime[50];
    char writebuf[100] = "timestamp:";
    size_t strtime_len;

    if (time(&t) == -1) {
        syslog(LOG_ERR, "Error getting time: %s", strerror(errno));
        return;
    }

    if (localtime_r(&t, &break_time) == NULL) {
        syslog(LOG_ERR, "Error converting time to human readable format: %s", strerror(errno));
        return;
    }

    strtime_len = strftime(outtime, sizeof(outtime), "%a, %d %b %Y %T %z", &break_time);

    if (strtime_len == 0) {
        syslog(LOG_ERR, "Error formatting time");
        return;
    }

    outtime[strtime_len] = '\n';
    strncat(writebuf, outtime, strtime_len + 1);

    rc = pthread_mutex_lock(&file_mutex);
    if (rc != 0) {
        syslog(LOG_ERR, "Error acquiring file mutex");
        return;
    }

    if (write(filefd_for_time, writebuf, strlen(writebuf)) == -1) {
        syslog(LOG_ERR, "Error writing time to file: %s", strerror(errno));
    }
    
    rc = pthread_mutex_unlock(&file_mutex);
    if (rc != 0) {
        syslog(LOG_ERR, "Error unlocking file mutex");
        return;
    }
}

void signal_handler(int sig_num) {
    if (sig_num == SIGINT || sig_num == SIGTERM) {
        terminate = true;
    } else if (sig_num == SIGALRM) {
        print_time();
    }
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

static void *thread_handle_conn(void *thread_params) {
    struct thread_conn_data *conn_params = (struct thread_conn_data*)thread_params;
    char *readbuf = conn_params->read_buffer;
    char *writebuf = conn_params->write_buffer;
    int rc;

    ssize_t recv_bytes, written_bytes, read_bytes, send_bytes;
    bool packet_received = false;

    
    syslog(LOG_INFO, "Accepted connection from %s", conn_params->conn_ip);

    if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0) {
        syslog(LOG_ERR, "Error setting thread cancel state");
        conn_params->thread_complete_success = false;
        return thread_params;
    }

    rc = pthread_mutex_lock(conn_params->mutex);
    if (rc != 0) {
        syslog(LOG_ERR, "Error acquiring mutex");
        conn_params->thread_complete_success = false;
        return thread_params;
    }
    bool packet_written = true;
    while (!packet_received) {
        recv_bytes = recv(conn_params->connfd, readbuf, BUFFER_SIZE - 1, 0);
        if (recv_bytes == -1) {
            syslog(LOG_ERR, "recv() error: %s", strerror(errno));
            packet_written = false;
            break;
        } else if (recv_bytes == 0) {
            syslog(LOG_DEBUG, "No more bytes to read from client");
            break;
        }
        readbuf[recv_bytes] = '\0';
        if (readbuf[recv_bytes - 1] == '\n') {
            packet_received = true;
        }

        // Handle ioctl command
        if (strncmp(readbuf, "AESDCHAR_IOCSEEKTO", 18) == 0) {
            struct aesd_seekto seekto;
            char *comma, *colon;
            syslog(LOG_INFO, "AESDCHAR_IOCSEEKTO ioctl ccommand received, %s", readbuf);
            if ((colon = strchr(readbuf, ':')) == NULL) {
                syslog(LOG_ERR, "Colon not found in AESDCHAR_IOCSEEKTO ioctl string");
                continue;
            } else {
                seekto.write_cmd = (uint32_t)strtoul(colon + 1, NULL, 0);
            }
            if ((comma = strchr(readbuf, ',')) == NULL) {
                syslog(LOG_ERR, "Comma not found in AESDCHAR_IOCSEEKTO ioctl string");
            } else {
                seekto.write_cmd_offset = (uint32_t)strtoul(comma + 1, NULL, 0);
            }
            syslog(LOG_INFO, "Sending AESDCHAR_IOCSEEKTO ioctl with args %u and %u", seekto.write_cmd, seekto.write_cmd_offset);
            if (ioctl(conn_params->readfd, AESDCHAR_IOCSEEKTO, &seekto) == -1) {
                syslog(LOG_ERR, "ioctl error: %s", strerror(errno));
            }
            continue;
        }

        written_bytes = write(conn_params->writefd, readbuf, recv_bytes);
        if (written_bytes == -1) {
            syslog(LOG_ERR, "write() error: %s", strerror(errno));
            packet_written = false;
            break;
        } else if (written_bytes < recv_bytes) {
            syslog(LOG_ERR, "Could not write %ld bytes, written bytes: %ld", recv_bytes, written_bytes);
        } else {
            syslog(LOG_INFO, "Written %ld bytes: %s", written_bytes, readbuf);
        }
    }
    rc = pthread_mutex_unlock(conn_params->mutex);
    if (rc != 0) {
        syslog(LOG_ERR, "Error unlocking mutex");
        conn_params->thread_complete_success = false;
        return thread_params;
    }
    if (!packet_written) {
        conn_params->thread_complete_success = false;
        return thread_params;
    }
    bool file_content_sent = true;
    int read_pos = 0;
    rc = pthread_mutex_lock(conn_params->mutex);
    if (rc != 0) {
        syslog(LOG_ERR, "Error acquiring mutex");
        conn_params->thread_complete_success = false;
        return thread_params;
    }
    while (true) {
        read_bytes = read(conn_params->readfd, writebuf, BUFFER_SIZE);
        if (read_bytes == -1) {
            syslog(LOG_ERR, "read() error: %s", strerror(errno));
            file_content_sent = false;
            break;
        } else if (read_bytes == 0) {
            syslog(LOG_DEBUG, "No more bytes to read from file");
            break;
        }
        read_pos += read_bytes;
        send_bytes = send(conn_params->connfd, writebuf, read_bytes, 0);
        if (send_bytes == -1) {
            syslog(LOG_ERR, "send() error: %s", strerror(errno));
            file_content_sent = false;
            break;
        } else {
            syslog(LOG_INFO, "Sent %ld bytes to %s", send_bytes, conn_params->conn_ip);
        }
    }
    rc = pthread_mutex_unlock(conn_params->mutex);
    if (rc != 0) {
        syslog(LOG_ERR, "Error unlocking mutex");
        conn_params->thread_complete_success = false;
        return thread_params;
    }
    if (!file_content_sent) {
        conn_params->thread_complete_success = false;
        return thread_params;
    }

    conn_params->thread_complete_success = true;
    return thread_params;
}

static void cleanup_thread_list(struct slisthead *head) {
    struct list_entry *node, *next_node;
    int tryjoin_rtn = 0;
    void *thread_rtn = NULL;

    node = SLIST_FIRST(head);
    while (node != NULL) {
        tryjoin_rtn = pthread_tryjoin_np(node->thread_id, &thread_rtn);
        next_node = SLIST_NEXT(node, entries);
        if (tryjoin_rtn == 0) {
            free(node->conn_data->read_buffer);
            free(node->conn_data->write_buffer);
            syslog(LOG_INFO, "Closed connection from %s", node->conn_data->conn_ip);
            shutdown(node->conn_data->connfd, SHUT_RDWR);
            close(node->conn_data->connfd);
            close(node->conn_data->readfd);
            close(node->conn_data->writefd);
            free(node->conn_data);
            SLIST_REMOVE(head, node, list_entry, entries);
            free(node);
        }
        node = next_node;
    } 
}

static void close_connections(struct slisthead *head) {
    struct list_entry *node, *next_node;

    node = SLIST_FIRST(head);
    while (node != NULL) {
        next_node = SLIST_NEXT(node, entries);
        pthread_cancel(node->thread_id);
        free(node->conn_data->read_buffer);
        free(node->conn_data->write_buffer);
        syslog(LOG_INFO, "Closed connection from %s", node->conn_data->conn_ip);
        shutdown(node->conn_data->connfd, SHUT_RDWR);
        close(node->conn_data->connfd);
        close(node->conn_data->readfd);
        close(node->conn_data->writefd);
        free(node->conn_data);
        SLIST_REMOVE(head, node, list_entry, entries);
        free(node);
        node = next_node;
    } 
}

int main(int argc, char* argv[]) {
    openlog(NULL, 0, LOG_USER);

    int status, poll_rtn;
    int readfd, writefd, newfd;
    socklen_t sin_size;
    struct sockaddr_storage their_addr;
    struct addrinfo *servinfo, hints;
    struct sigaction new_action;
    pthread_t conn_thread;
    struct thread_conn_data *thread_data;
    struct list_entry *n;
    struct slisthead head;
    struct pollfd poll_data;
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
        cleanup();
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
        if (setsid() < 0) {
            syslog(LOG_ERR, "Failed to set sid: %s", strerror(errno));
            fork_success = false;;
        }
        int nullfd;
        nullfd = open("/dev/null", O_RDWR);
        if (nullfd == -1) {
            syslog(LOG_ERR, "Failed to open /dev/null: %s", strerror(errno));
            fork_success = false;
        }
        if (
            dup2(nullfd, STDOUT_FILENO) < 0 ||
            dup2(nullfd, STDIN_FILENO) < 0 ||
            dup2(nullfd, STDERR_FILENO) < 0
        ) {
            syslog(LOG_ERR, "dup2() error: %s", strerror(errno));
            fork_success = false;
        } else {
            close(nullfd);
            close(STDOUT_FILENO);
            close(STDIN_FILENO);
            close(STDERR_FILENO);
        }
    }

    if (!fork_success) {
        cleanup();
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
	if (sigaction(SIGALRM, &new_action, NULL) != 0) {
		syslog(LOG_ERR, "Error %d (%s) registering for SIGALRM\n", errno, strerror(errno));
        success = false;
	}

    if (pthread_mutex_init(&file_mutex, NULL) != 0) {
        syslog(LOG_ERR, "Error initializing file mutex");
        success = false;
    }

#if !USE_AESD_CHAR_DEVICE
    filefd_for_time = open(SOCKFILE, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (filefd_for_time == -1) {
        syslog(LOG_ERR, "open() error: %s", strerror(errno));
        success = false;
    }

    struct itimerval delay;
    delay.it_value.tv_sec = 10;
    delay.it_value.tv_usec = 0;
    delay.it_interval.tv_sec = 10;
    delay.it_interval.tv_usec = 0;

    if (setitimer(ITIMER_REAL, &delay, NULL) != 0) {
        syslog(LOG_ERR, "setitimer() error: %s", strerror(errno));
        success = false;
    }
#endif

    if (!success) {
        syslog(LOG_ERR, "Error initalizing socket server");
        cleanup();
        closelog();
        return -1;
    }

    poll_data.fd = sockfd;
    poll_data.events = POLLIN;

    SLIST_INIT(&head);

    while (!terminate) {
        poll_rtn = poll(&poll_data, 1, -1);

        if (poll_rtn == -1) {
            syslog(LOG_ERR, "poll() error: %s", strerror(errno));
            continue;
        } else if (poll_rtn == 1) {
            sin_size = sizeof(their_addr);
            if ((newfd = accept(sockfd, (struct sockaddr*)&their_addr, &sin_size)) == -1) {
                syslog(LOG_ERR, "Accept error: %s", strerror(errno));
                continue;
            }

            memset(s, 0, INET6_ADDRSTRLEN);
            inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr*)&their_addr), s, sizeof(s));
            
            readfd = open(SOCKFILE, O_RDONLY);
            if (readfd == -1) {
                syslog(LOG_ERR, "open() error: %s", strerror(errno));
                continue;
            }
            
            writefd = open(SOCKFILE, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (writefd == -1) {
                syslog(LOG_ERR, "open() error: %s", strerror(errno));
                continue;
            }
            
            char *readbuf = (char*)malloc(BUFFER_SIZE * sizeof(char));
            if (readbuf == NULL) {
                syslog(LOG_ERR, "Malloc error for read buffer: %s", strerror(errno));
                continue;
            }
            memset(readbuf, 0, BUFFER_SIZE * sizeof(char));
            char *writebuf = (char*)malloc(BUFFER_SIZE * sizeof(char));
            if (writebuf == NULL) {
                syslog(LOG_ERR, "Malloc error for write buffer: %s", strerror(errno));
                free(readbuf);
                continue;
            }
            memset(writebuf, 0, BUFFER_SIZE * sizeof(char));

            thread_data = (struct thread_conn_data*)malloc(sizeof(struct thread_conn_data));
            if (thread_data == NULL) {
                syslog(LOG_ERR, "Malloc error for thread data: %s", strerror(errno));
                free(readbuf);
                free(writebuf);
                continue;
            }
            thread_data->connfd = newfd;
            thread_data->readfd = readfd;
            thread_data->writefd = writefd;
            thread_data->conn_ip = s;
            thread_data->read_buffer = readbuf;
            thread_data->write_buffer = writebuf;
            thread_data->mutex = &file_mutex;
            thread_data->thread_complete_success = false;

            pthread_create(&conn_thread, NULL, thread_handle_conn, thread_data);

            n = (struct list_entry*)malloc(sizeof(struct list_entry));
            if (n == NULL) {
                syslog(LOG_ERR, "Malloc error for list entry");
                free(readbuf);
                free(writebuf);
                free(thread_data);
                continue;
            }
            n->thread_id = conn_thread;
            n->conn_data = thread_data;
            SLIST_INSERT_HEAD(&head, n, entries);
        } 
        cleanup_thread_list(&head);
    }
    
    syslog(LOG_INFO, "Caught signal, exiting");
    printf("Caught signal, exiting\n");
    close_connections(&head);
    cleanup();
    closelog();

    return 0;
}