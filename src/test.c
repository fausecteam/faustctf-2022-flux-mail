#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "include/server.h"
#include "include/util.h"


int main(void) {
	server_loop();
	exit(0);
}
