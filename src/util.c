#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#include "include/util.h"

void die(char *name) {
	perror(name);
	printf("Well, seems like there is some kind of a problem. \n");
	printf("Please try again later.\n");
	fflush(stdout);
	exit(1);
}

void graceful_shutdown(char *name, struct data server) {
	perror(name);
	printf("Well, seems like there is some kind of a problem. \n");
	printf("Please try again later.\n");
	fflush(stdout);
	(void)!write(server.fd_to, "die\n", 5);
	exit(1);
}
