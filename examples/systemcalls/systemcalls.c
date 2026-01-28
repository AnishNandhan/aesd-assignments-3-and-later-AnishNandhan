#include "systemcalls.h"
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    int rc = system(cmd);

    if (rc != 0) {
        return false;
    }

    return true;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    int cpid = fork();

    if (cpid == -1) {
        perror("fork() error");
        va_end(args);
        return false;
    }

    fflush(stdout);

    if (cpid == 0) {
        int ret;
        ret = execv(command[0], command);
        if (ret == -1) {
            perror("execv() failure");
            _exit(EXIT_FAILURE);
        }
    }

    int status, pid;
    bool ret = true;
    pid = wait(&status);
        
    if (pid == -1) {
        perror("wait() error");
        ret = false;
    }

    if (!WIFEXITED(status)) {
        fprintf(stderr, "Process %d did not exit properly\n", pid);
        ret = false;
    }

    if (WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Process %d exited with non-zero exit code: %d\n", pid, status);
        ret = false;
    }

    va_end(args);

    return ret;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/
    int fd, cpid;

    fd = open(outputfile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH | S_IRGRP);

    if (fd == -1) {
        perror("Failed to open file");
        va_end(args);
        return false;
    }

    cpid = fork();

    if (cpid == -1) {
        perror("fork() error");
        va_end(args);
        return false;
    }

    fflush(stdout);

    if (cpid == 0) {
        if (dup2(fd, 1) == -1) {
            perror("dup2() error");
            _exit(EXIT_FAILURE);
        }
        close(fd);
        int ret;
        ret = execv(command[0], command);
        if (ret == -1) {
            perror("execv() failure");
            _exit(EXIT_FAILURE);
        }
    }

    close(fd);

    int status, pid;
    bool ret = true;

    pid = wait(&status);

    if (pid == -1) {
        perror("wait() error");
        ret = false;
    }

    if (!WIFEXITED(status)) {
        fprintf(stderr, "Process %d did not exit properly\n", pid);
        ret = false;
    }

    if (WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Process %d exited with non-zero exit code: %d\n", pid, status);
        ret = false;
    }

    va_end(args);

    return ret;
}
