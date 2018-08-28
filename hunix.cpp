#ifdef __unix__

#include "hunix.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

void daemonize(){
    pid_t pid = fork();
    if (pid < 0){
        printf("fork error: %d", errno);
        return;
    }

    if (pid > 0){
        // exit parent process
        exit(0);
    }

    // child process become process leader
    setsid();

    // set fd
    int fd;
    if ((fd = open("/dev/null", O_RDWR, 0)) != -1){
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO)
			close(fd);
    }
}

#endif // __unix__