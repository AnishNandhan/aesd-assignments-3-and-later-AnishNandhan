#include <fcntl.h>
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char* argv[]) {
    openlog(NULL, 0, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR, "Wrong number of arguments passed: %d. Expected 2", argc - 1);
        exit(1);     // referred from https://stackoverflow.com/questions/2425167/use-of-exit-function
    }

    int fd;
    fd = creat(argv[1], S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);    // Assuming parent directory already exists

    if (fd == -1) {
        int err1 = errno;
        syslog(LOG_ERR, "Error while creating or opening file: %s", strerror(err1));
        exit(1);
    }

    int lenToWrite = strlen(argv[2]);    // Reference: https://www.man7.org/linux/man-pages/man3/strlen.3.html

    syslog(LOG_DEBUG, "Writing %s to file %s", argv[2], argv[1]);
    int nr = write(fd, argv[2], lenToWrite);

    if (nr == -1) {
        int err2 = errno;
        syslog(LOG_ERR, "Error while writing to file %s", strerror(err2));
        exit(1);
    }

    if (nr != lenToWrite) {
        int err3 = errno;
        syslog(LOG_ERR, "Could not write entire input to file");
        exit(1);
    }

    closelog();

    return 0;
}