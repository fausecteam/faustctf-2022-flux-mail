#ifndef FLUX_TYPES
#define FLUX_TYPES
#include <stdio.h>

struct data {
	// in and out pipes for comm. with server
	int fd_to;
	int fd_from;
	// include user information
	int userid;
	char *username;
	// include the mmaped file
	char *mmap_region;
};

struct mail {
	char *from;
	char *to;
	char *message;
};

struct user {
	int userid;
	char *username;
	char *password;
	char *mmap_region;
};

#endif
