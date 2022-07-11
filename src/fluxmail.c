#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "include/client.h"
#include "include/server.h"
#include "include/util.h"


int main(void) {
	// before forking, establish pipes for communication
	int pipe_to_server[2];
	if (pipe(pipe_to_server)) {
		die("pipe");
	}
	int pipe_from_server[2];
	if (pipe(pipe_from_server)) {
		die("pipe");
	}
	// fork for background task
	pid_t p = fork();
	if (p < 0) {
		die("fork");
	} else if (p == 0) {
		// close unnecessary pipe ends
		// ind 1 is for write -> close in to_server
		close(pipe_to_server[1]);
		// ind 0 is for read -> close in from_server
		close(pipe_from_server[0]);

		// overwrite stdin and stdout with pipes
		int fd = dup2(pipe_to_server[0], STDIN_FILENO);
		if (fd < 0) {
			die("dup2");
		}
		close(pipe_to_server[0]);

		fd = dup2(pipe_from_server[1], STDOUT_FILENO);
		if (fd < 0) {
			die("dup2");
		}
		close(pipe_from_server[1]);

		server_loop();
		exit(0);
	} else {
		// start client code

		// close unnecessary pipe ends
		// ind 0 is read end -> close in to_server
		close(pipe_to_server[0]);
		// ind 1 is write end -> close in from_server
		close(pipe_from_server[1]);

		struct data server;
		server.fd_to = pipe_to_server[1];
		server.fd_from = pipe_from_server[0];

		client_loop(server);
	}
	// send poison pill to background
	if (write(pipe_to_server[1], "die\n", 5) < 0) {
		return 1;
	}
	return 0;
}
