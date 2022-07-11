#define _POSIX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

#include "include/flux-types.h"
#include "include/server.h"

static void inbox(struct user *usr);
static void outbox(struct user *usr);
static void send(struct user *usr);
//static void delete(struct user *usr);
static struct user *login(void);
static struct user *create(void);
static int read_stdin(int len, char *buf);

void server_loop(void) {
	struct user *user = NULL;
	char line[50];
	while (42) {
		// read char from stdin
		if (read_stdin(50, line) != 0) {
			if (write(STDOUT_FILENO, "f\n", 2) == -1) {
				exit(1);
			}
			return;
		}

		// check for poison pill
		if (strcmp(line, "die\n") == 0) {
			return;
		}

		// dispatch to handler function
		if (strcmp(line, "c\n") == 0) {
			user = create();
		} else if (strcmp(line, "l\n") == 0) {
			user = login();
		} else if (strcmp(line, "vi\n") == 0) {
			inbox(user);
		} else if (strcmp(line, "vo\n") == 0) {
			outbox(user);
		} else if (strcmp(line, "s\n") == 0) {
			send(user);
		} else if (strcmp(line, "lo\n") == 0) {
			free(user->password);
			free(user->username);
			free(user);
			user = NULL;
		}
	}
}

static int read_stdin(int len, char *buf) {
	int count = 0;
	while (count < len) {
		char c;
		int ret = read(STDIN_FILENO, &c, 1);
		if (ret == -1) {
			perror("read");
			return -1;
		}
		if (ret == 0) {
			break;
		}

		buf[count] = c;
		++count;
		if (c == '\n') {
			break;
		}
		if (c == '\0') {
			--count;
		}
	}
	if (count < len) {
		buf[count] = '\0';
	} else {
		buf[len - 1] = '\0';
	}
	return 0;
}

static void write_box_to_out(char *filename, struct user *usr) {
	struct mail *mail_list = malloc(sizeof(struct mail) * 23);
	if (mail_list == NULL) {
		if (write(STDOUT_FILENO, "f\n", 2) == -1) {
			perror("write");
		}
		exit(1);
	}
	if (access(filename, F_OK) != 0) {
		free(mail_list);
		if (write(STDOUT_FILENO, "e\n", 2) == -1) {
			perror("write");
			exit(1);
		}
		return;
	}
	FILE *fp = fopen(filename, "r");
	if (fp == NULL) {
		free(mail_list);
		if (write(STDOUT_FILENO, "f\n", 2) == -1) {
			perror("write");
			exit(1);
		}
		return;
	}
	int ind = 0;
	// read mails until EOF
	// treat mail_list as a ring buffer
	char from[50];
	char message[121];
	while (1 < 2) {
		if (fgets(from, 50, fp) == NULL) {
			// Could be eof or error
			if (feof(fp)) {
				break;
			}
			if (write(STDOUT_FILENO, "f\n", 2) == -1) {
				perror("write");
				exit(1);
			}
			for (int a = 0; a < 23 && a < ind; ++a) {
				free(mail_list[a].from);
				free(mail_list[a].message);
			}
			free(mail_list);
			return;
		}
		if (fgets(message, 121, fp) == NULL) {
			if (write(STDOUT_FILENO, "f\n", 2) == -1) {
				perror("write");
				exit(1);
			}
			for (int a = 0; a < 23 && a < ind; ++a) {
				free(mail_list[a].from);
				free(mail_list[a].message);
			}
			free(mail_list);
			return;
		}
		if (ind >= 23) {
			free(mail_list[ind%23].from);
		}
		mail_list[ind%23].from = malloc(sizeof(char) * 50);
		if (mail_list[ind%23].from == NULL) {
			if (write(STDOUT_FILENO, "f\n", 2) == -1) {
				perror("write");
				exit(1);
			}
			for (int a = 0; a < 23 && a < ind; ++a) {
				free(mail_list[a].from);
				free(mail_list[a].message);
			}
			free(mail_list);
			return;
		}
		if (ind >= 23) {
			free(mail_list[ind%23].message);
		}
		mail_list[ind%23].message = malloc(sizeof(char) * 121);
		if (mail_list[ind%23].message == NULL) {
			if (write(STDOUT_FILENO, "f\n", 2) == -1) {
				perror("write");
				exit(1);
			}
			for (int a = 0; a < 23 && a < ind; ++a) {
				free(mail_list[a].from);
				free(mail_list[a].message);
			}
			free(mail_list);
			return;
		}
		strcpy(mail_list[ind%23].from, from);
		strcpy(mail_list[ind%23].message, message);
		++ind;
	}
	fclose(fp);
	if (ind == 0) {
		if (write(STDOUT_FILENO, "e\n", 2) == -1) {
			perror("write");
			exit(1);
		}
		return;
	}
	// make sure to keep track of where the oldest mail lies
	int oldest = (ind >= 23) ? (ind) % 23 : 0;
	int num = (ind >= 23) ? 23 : ind;

	// write data to file
	int offset = 0;
	sprintf(usr->mmap_region, "%d\n", num);
	offset = strlen(usr->mmap_region);
	char *ptr = NULL;
	int cur_ind = 0;
	for (int i = 0; i < num; ++i) {
		cur_ind = (i + oldest) % 23;
		ptr = usr->mmap_region + offset;
		strcpy(ptr, mail_list[cur_ind].from);
		offset += strlen(mail_list[cur_ind].from);
		ptr = usr->mmap_region + offset;
		strcpy(ptr, mail_list[cur_ind].message);
		offset += strlen(mail_list[cur_ind].message);
		free(mail_list[cur_ind].message);
		free(mail_list[cur_ind].from);
	}
	free(mail_list);

	// signal client
	if (write(STDOUT_FILENO, "o\n", 2) == -1) {
		perror("write");
		exit(1);
	}
}

static void inbox(struct user *usr) {
	if (usr == NULL) {
		if (write(STDOUT_FILENO, "f\n", 2) == -1)
			perror("write");
		return;
	}
	// get magic numer and user
	int offs;
	char line[12];
	if (read_stdin(12, line) != 0) {
		if (write(STDOUT_FILENO, "f\n", 2) == -1)
			perror("write");
		exit(1);
	}

	sscanf(line, "%d\n", &offs);

	char name[50];
	strcpy(name, usr->mmap_region + offs);
	if (name[strlen(name)-1] == '\n') {
		name[strlen(name)-1] = '\0';
	}

	/*
	 * read users inbox
	 * Since one message can max take about 175 bytes, we can
	 * transfer about 23 messages. Take the 23 newest ones.
	 */
	char filename[5 + 50 + 6];
	sprintf(filename, "data/inbox-%s", name);
	write_box_to_out(filename, usr);
}

static void outbox(struct user *usr) {
	if (usr == NULL) {
		if (write(STDOUT_FILENO, "f\n", 2) == -1)
			perror("write");
		return;
	}
	// get magic numer and user
	int offs;
	char line[12];
	if (read_stdin(12, line) != 0) {
		if (write(STDOUT_FILENO, "f\n", 2) == -1)
			perror("write");
		exit(1);
	}

	sscanf(line, "%d\n", &offs);

	char name[50];
	strcpy(name, usr->mmap_region + offs);
	if (name[strlen(name)-1] == '\n') {
		name[strlen(name)-1] = '\0';
	}

	// read users outbox
	// write data to file
	char filename[5 + 50 + 7];
	sprintf(filename, "data/outbox-%s", name);
	write_box_to_out(filename, usr);
}

static void send(struct user *usr) {
	if (usr == NULL) {
		if (write(STDOUT_FILENO, "f\n", 2) == -1)
			perror("write");
		return;
	}
	// get magic number, sender and recipient
	int offs;
	char line[12];
	if (read_stdin(12, line) != 0) {
		if (write(STDOUT_FILENO, "f\n", 2) == -1)
			perror("write");
		exit(1);
	}

	sscanf(line, "%d\n", &offs);

	char name[50];
	strcpy(name, usr->mmap_region + offs);
	offs += strlen(name) + 1;
	if (name[strlen(name)-1] == '\n') {
		name[strlen(name)-1] = '\0';
	}

	char message[121];
	strcpy(message, usr->mmap_region + offs);
	if (message[strlen(message)-1] == '\n') {
		message[strlen(message)-1] = '\0';
	}

	// add mail to inbox of recipient and outbox of sender
	char file_from[5 + 50 + 7];
	sprintf(file_from, "data/outbox-%s", usr->username);
	char file_to[5 + 50 + 6];
	sprintf(file_to, "data/inbox-%s", name);

	FILE *f_out = fopen(file_from, "a");
	if (f_out == NULL) {
		if (write(STDOUT_FILENO, "f\n", 2) == -1)
			perror("write");
		exit(1);
	}
	FILE *f_in = fopen(file_to, "a");
	if (f_in == NULL) {
		fclose(f_out);
		if (write(STDOUT_FILENO, "f\n", 2) == -1)
			perror("write");
		exit(1);
	}

	fprintf(f_out, "%s\n%s\n", name, message);
	fprintf(f_in, "%s\n%s\n", usr->username, message);
	fclose(f_out);
	fclose(f_in);

	// send ack
	if (write(STDOUT_FILENO, "o\n", 2) == -1) {
		perror("write");
		exit(1);
	}
}

static int read_creds(char *name, char *pass) {
	if (read_stdin(50, name) != 0) {
		if (write(STDOUT_FILENO, "f\n", 2) == -1) {
			perror("write");
			exit(1);
		}
		return 0;
	} // I trust the client to send the correct lengths

	if (name[strlen(name)-1] == '\n') {
		name[strlen(name)-1] = '\0';
	}
	if (read_stdin(50, pass) != 0) {
		if (write(STDOUT_FILENO, "f\n", 2) == -1) {
			perror("write");
			exit(1);
		}
		return 0;
	}

	return 1;
}

static struct user *load_user_info(char *name) {
	// TODO error cases are not distinguishable from non existing user
	char filename[5 + 50];
	sprintf(filename, "data/%s", name);
	/*
	 * File Format:
	 *
	 *   password\n
	 *   userid\n
	 */
	FILE *fp = fopen(filename, "r");
	if (fp == NULL) {
		return NULL;
	}

	// construct user struct
	struct user *usr = malloc(sizeof(struct user));
	if (usr == NULL) {
		return NULL;
	}
	usr->username = name;
	usr->password = malloc(sizeof(char) * 50);
	if (usr->password == NULL) {
		free(usr);
		return NULL;
	}
	if (fgets(usr->password, 50, fp) == NULL) {
		free(usr->password);
		free(usr);
		return NULL;
	}
	char line[12];
	if (fgets(line, 12, fp) == NULL) {
		free(usr->password);
		free(usr);
		return NULL;
	}
	if (sscanf(line, "%d\n", &(usr->userid)) == EOF) {
		free(usr->password);
		free(usr);
		return NULL;
	}

	// open mmaped file
	char mmap_name[20];
	snprintf(mmap_name, 20, "data/%d", usr->userid);
	int fd = open(mmap_name, O_RDWR);
	if (fd < 0) {
		free(usr->password);
		free(usr);
		return NULL;
	}
	char *region = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (region == (char *) -1) {
		close(fd);
		free(usr->password);
		free(usr);
		return NULL;
	}
	close(fd);
	usr->mmap_region = region;

	return usr;
}

static struct user *login(void) {
	// get user information
	char pass[50];
	char *name = malloc(sizeof(char) * 50);
	if (name == NULL) {
		if (write(STDOUT_FILENO, "f\n", 2) == -1)
			perror("write");
		exit(1);
	}
	if (!read_creds(name, pass)) {
		exit(1);
	}

	// check user information
	struct user *usr = load_user_info(name);
	if (usr == NULL || strcmp(pass, usr->password) != 0) {
		if (write(STDOUT_FILENO, "i\n", 2) == -1) {
			perror("write");
			exit(1);
		}
		free(name);
		return NULL;
	}

	// send positive result
	if (write(STDOUT_FILENO, "o\n", 2) == -1) {
		perror("write");
		exit(1);
	}

	char id[12];
	snprintf(id, 12, "%d\n", usr->userid);
	if (write(STDOUT_FILENO, id, strlen(id)) == -1) {
		exit(1);
	}

	return usr;
}

static struct user *create_user(char *name, char *pass) {
	// figure out user id
	int nextid = 0;
	errno = 0;
	FILE *fp = fopen("data/nextid", "r+");
	if (fp == NULL) {
		if (errno == ENOENT) {
			fp = fopen("data/nextid", "w+");
			if (fp == NULL) {
				return NULL;
			}
		} else {
			return NULL;
		}
	}
	int file_d = fileno(fp);
	if (file_d == -1) {
		return NULL;
	}
	errno = 0;
	while (flock(file_d, LOCK_EX) == -1) {
		if (errno != EINTR)
			return NULL;
		errno = 0;
	}

	char line[12];
	if (fgets(line, 12, fp) == NULL) {
		if (feof(fp)) {
			fprintf(fp, "%d", nextid + 1);
			fflush(fp);
		} else {
			fclose(fp);
			return NULL;
		}
	} else {
		sscanf(line, "%d", &nextid);
		if (fseek(fp, 0, SEEK_SET) < 0) {
			fclose(fp);
			return NULL;
		}
		fprintf(fp, "%d", nextid + 1);
		fflush(fp);
	}

	errno = 0;
	while (flock(file_d, LOCK_UN) == -1) {
		if (errno != EINTR)
			return NULL;
		errno = 0;
	}
	fclose(fp);

	// TODO
	/*
	if (access("data/nextid", F_OK) == 0) {
		// file exists, read next id
		FILE *fp = fopen("data/nextid", "r+");
		if (fp == NULL) {
			return NULL;
		}
		char line[12];
		if (fgets(line, 12, fp) == NULL) {
			fclose(fp);
			return NULL;
		}
		sscanf(line, "%d", &nextid);
		if (fseek(fp, 0, SEEK_SET) < 0) {
			fclose(fp);
			return NULL;
		}
		fprintf(fp, "%d", nextid + 1);
		fflush(fp);
		fclose(fp);
	} else {
		// file does not exist, create set correctly
		FILE *fp = fopen("data/nextid", "w");
		if (fp == NULL) {
			return NULL;
		}
		fprintf(fp, "%d", nextid + 1);
		fflush(fp);
		fclose(fp);
	}
	*/

	// init struct
	struct user *usr = malloc(sizeof(struct user));
	if (!usr) {
		return NULL;
	}
	usr->password = malloc(sizeof(char) * 50);
	if (!usr->password) {
		free(usr);
		return NULL;
	}
	strcpy(usr->password, pass);
	usr->username = name;
	usr->userid = nextid;

	// open mmaped file
	char mmap_name[20];
	snprintf(mmap_name, 20, "data/%d", usr->userid);
	// resize file to 4096B
	FILE *fp_mmap = fopen(mmap_name, "w+");
	if (fp_mmap == NULL) {
		free(usr->password);
		free(usr);
		return NULL;
	}
	int size = 4096;
	if (fseek(fp_mmap, size - 1, SEEK_SET) == -1) {
		free(usr->password);
		free(usr);
		fclose(fp_mmap);
		return NULL;
	}
	fputc('\0', fp_mmap);
	fclose(fp_mmap);

	int fd = open(mmap_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		free(usr->password);
		free(usr);
		return NULL;
	}
	char *region = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (region == (char *) -1) {
		close(fd);
		free(usr->password);
		free(usr);
		return NULL;
	}
	close(fd);
	usr->mmap_region = region;

	/* write to disc and return
	 * File Format:
	 *
	 *   password\n
	 *   userid\n
	 */
	char filename[5 + 50];
	sprintf(filename, "data/%s", name);
	FILE *file_p = fopen(filename, "w");
	if (file_p == NULL) {
		munmap(usr->mmap_region, 4096);
		free(usr->password);
		free(usr);
		return NULL;
	}
	fprintf(file_p, "%s%d\n", usr->password, usr->userid);
	fflush(file_p);
	fclose(file_p);

	return usr;
}

static struct user *create(void) {
	// get info
	char pass[50];
	char *name = malloc(sizeof(char) * 50);
	if (name == NULL) {
		if (write(STDOUT_FILENO, "f\n", 2) == -1)
			perror("write");
		exit(1);
	}
	if (!read_creds(name, pass)) {
		exit(1);
	}

	// check for existing
	// check for the specific name first marty
	if (strcmp(name, "marty") == 0) {
		if (write(STDOUT_FILENO, "u\n", 2) == -1) {
			perror("write");
			exit(1);
		}
		free(name);
		return NULL;
	}

	struct user *usr = load_user_info(name);
	if (usr != NULL) {
		if (write(STDOUT_FILENO, "u\n", 2) == -1) {
			perror("write");
			exit(1);
		}
		free(name);
		return NULL;
	}

	// create user
	usr = create_user(name, pass);
	if (usr == NULL) {
		if (write(STDOUT_FILENO, "e\n", 2) == -1)
			perror("write");
		free(name);
		exit(1);
	}
	// send positive result
	if (write(STDOUT_FILENO, "o\n", 2) == -1) {
		exit(1);
	}
	char id[12];
	snprintf(id, 12, "%d\n", usr->userid);
	if (write(STDOUT_FILENO, id, strlen(id)) == -1) {
		exit(1);
	}

	return usr;
}
