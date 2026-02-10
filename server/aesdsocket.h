#include <stdbool.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/socket.h>

struct thread_conn_data {
    int connfd;
    char *conn_ip;
    char *read_buffer;
    char *write_buffer;
    pthread_mutex_t *mutex;
    bool thread_complete_success;
};

struct list_entry {
    pthread_t thread_id;
    struct thread_conn_data *conn_data;
    SLIST_ENTRY(list_entry) entries;
};

SLIST_HEAD(slisthead, list_entry);


