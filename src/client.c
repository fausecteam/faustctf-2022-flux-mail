#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "include/client.h"
#include "include/util.h"

static void authenticated_loop(struct data *server);
static void view_inbox(struct data *server);
static void view_outbox(struct data *server);
static void write_mail(struct data *server);
//static void delete_mail(struct data *server);
static int create_user(struct data *server);
static int login(struct data *server);

void client_loop(struct data server) {
	// print welcome message
	printf("\tWelcome to Flux Mail!\n\n");
	printf("\tSend messages across time! Easy, fast, and secure!\n");
	printf("\tWhat are you waiting for? Join right now and try it out yourself!\n\n");
	while (42) {
		// print menu
		printf("\tLogin menu:\n");
		printf("\t  (1) Create new user\n");
		printf("\t  (2) Login to existing user\n");
		printf("\t  (3) exit\n");
		printf("\t> ");
		fflush(stdout);

		char line[50];
		if (!fgets(line, 50, stdin)) {
			graceful_shutdown("fgets", server);
		}

		size_t len = strlen(line);
		if (len != 2) {
			printf("Type the number and press enter!\n");
			fflush(stdout);
			if (line[len-2] != '\n') {
				int c;
				while ((c = fgetc(stdin))!= '\n' && c != EOF);
				if (c == EOF) {
					break;
				}
			}
			continue;
		}
		// options:
		//  - create user
		//  - login -> go to logged in menu
		//  - exit
		if (line[0] == '1') {
			int ret = create_user(&server);
			if (ret > 0) {
				authenticated_loop(&server);
			} else if (ret == EOF) {
				break;
			}
		} else if (line[0] == '2') {
			int ret = login(&server);
			if (ret > 0) {
				authenticated_loop(&server);
			} else if (ret == EOF) {
				break;
			}
		} else if (line[0] == '3') {
			break;
		}
	}
}

static void authenticated_loop(struct data *server) {
	srand(time(0));
	while (42) {
		// print menu
		//
		// options:
		//  - view inbux
		//  - view outbox
		//  - write mail
		//  - logout
		printf("\n\tMenu:\n");
		printf("\t  (1) View inbox\n");
		printf("\t  (2) View outbox\n");
		printf("\t  (3) Write new mail\n");
		printf("\t  (4) logout\n");
		printf("\t> ");
		fflush(stdout);

		char line[50];
		if (!fgets(line, 50, stdin)) {
			graceful_shutdown("fgets", *server);
		}

		if (strlen(line) != 2) {
			printf("Type the number and press enter!\n");
			fflush(stdout);
			continue;
		}
		switch(line[0]) {
			case '1':
				view_inbox(server);
				break;
			case '2':
				view_outbox(server);
				break;
			case '3':
				write_mail(server);
				break;
			case '4':
				server->userid = -1;
				server->username = "";
				ssize_t ret = write(server->fd_to, "lo\n", 3); // I don't need the \0
				if (ret < 0) {
					graceful_shutdown("write", *server);
				}
				munmap(server->mmap_region, 4096);
				return;
			default:
				printf("Invalid input!\n");
				fflush(stdout);
		}
	}
}

static int get_credentials(char *name, char *pass) {
	printf("Username: ");
	fflush(stdout);
	if (!fgets(name, 50, stdin)) {
		return -2;
	}
	size_t len = strlen(name);
	if (len == 49 && name[48] != '\n') {
		printf("Name is too long!\n");
		fflush(stdout);
		int c;
		while ((c = fgetc(stdin))!= '\n' && c != EOF);
		if (c == EOF) {
			return c;
		}
		return 0;
	}
	printf("Password: ");
	fflush(stdout);
	if (!fgets(pass, 50, stdin)) {
		return -2;
	}
	len = strlen(name);
	if (len == 49 && pass[48] != '\n') {
		printf("Password is too long!\n");
		fflush(stdout);
		int c;
		while ((c = fgetc(stdin))!= '\n' && c != EOF);
		if (c == EOF) {
			return c;
		}
		return 0;
	}
	// TODO prevent directory traversal
	if (strchr(name, '.') != NULL || strchr(name, '/') != NULL) {
		printf("Bad characters in name!\n");
		fflush(stdout);
		return 0;
	}
	if (name[strlen(name)-1] == '\n') {
		name[strlen(name)-1] = '\0';
	}
	if (pass[strlen(pass)-1] == '\n') {
		pass[strlen(pass)-1] = '\0';
	}
	return 1;
}

static void send_credentials(struct data *server, char *cmd, char *name,
		char *pass) {
	ssize_t ret = write(server->fd_to, cmd, 2); // I don't need the \0
	if (ret < 0) {
		graceful_shutdown("write", *server);
	}
	ret = write(server->fd_to, name, strlen(name)); // I don't need the \0 to be written
	if (ret < 0) {
		graceful_shutdown("write", *server);
	}
	if (write(server->fd_to, "\n", 1) == -1) {
		graceful_shutdown("write", *server);
	}
	ret = write(server->fd_to, pass, strlen(pass)); // I don't need the \0
	if (ret < 0) {
		graceful_shutdown("write", *server);
	}
	if (write(server->fd_to, "\n", 1) == -1) {
		graceful_shutdown("write", *server);
	}
}

static int my_fgets(int fd, char *buf, size_t count) {
	char c = '\0';
	size_t i = 0;
	while (i < count && c != '\n') {
		if (read(fd, &c, 1) == -1) {
			return -1;
		}
		buf[i] = c;
		++i;
	}
	return i;
}

static int check_cred_response(struct data *server, char *name) {
	char resp[2];
	if (read(server->fd_from, resp, 2) == -1) {
		graceful_shutdown("read", *server);
	}
	if (resp[0] == 'o') {
		server->username = name;
		// read the user id
		char id[12];

		if (my_fgets(server->fd_from, id, 12) == -1) {
			graceful_shutdown("fgets", *server);
		}
		long uid = strtol(id, NULL, 10);
		if (uid < 0 || uid >= INT_MAX) {
			printf("Internal error\n");
			fflush(stdout);
			graceful_shutdown(NULL, *server);
		}
		server->userid = (int) uid;

		// open the mmaped file
		char filename[20];
		snprintf(filename, 20, "data/%d", server->userid);
		int fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
		if (fd < 0) {
			graceful_shutdown("open", *server);
		}
		char *region = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		if (region == (char *) -1) {
			graceful_shutdown("mmap", *server);
		}
		close(fd);
		server->mmap_region = region;

		return 1;
	} else if (resp[0] == 'i') {
		printf("Invalid credentials!\n");
		fflush(stdout);
		return 0;
	} else if (resp[0] == 'u') {
		printf("User is already in use!\n");
		fflush(stdout);
		return 0;
	} else {
		printf("Server error, please try again later\n");
		fflush(stdout);
		return -1;
	}
	return 0;
}

static int create_user(struct data *server) {
	// ask for user name and password
	char name[50];
	char password[50];
	int status = get_credentials(name, password);
	if (status == -2) {
		graceful_shutdown("fgets", *server);
	} else if (status <= 0) {
		return status;
	}

	// send data to server
	send_credentials(server, "c\n", name, password);

	// check response and act accordingly
	return check_cred_response(server, name);
}

static int login(struct data *server) {
	// ask for user name and password
	char name[50];
	char password[50];
	int status = get_credentials(name, password);
	if (status == -2) {
		graceful_shutdown("fgets", *server);
	} else if (status <= 0) {
		return status;
	}

	// send data to server
	send_credentials(server, "l\n", name, password);

	// check response and act accordingly
	return check_cred_response(server, name);
}

static void read_line_from_mmap(struct data *server, int len, char *out, int offset) {
	char fmt_string[10]; // enough for up to 4 digits
	snprintf(fmt_string, 10, "%%%d[^\n]", len); // %% is escape for %
	if (sscanf(server->mmap_region + offset, fmt_string, out) == EOF) {
		graceful_shutdown("sscanf", *server);
	}
}

static void signal_server(struct data *server, char *cmd, int offs) {
	char str_offs[9];
	snprintf(str_offs, 9, "%d\n", offs);

	printf("Whenever you're ready, press ENTER!\n");
	fflush(stdout);
	char buff[50];
	if (!fgets(buff, 50, stdin)) {
		graceful_shutdown("fgets", *server);
	}
	ssize_t ret = write(server->fd_to, cmd, strlen(cmd));
	if (ret < 0) {
		graceful_shutdown("write", *server);
	}
	ret = write(server->fd_to, str_offs, strlen(str_offs)); // NOTE: the number cannor have more than 3 digits
	if (ret < 0) {
		graceful_shutdown("write", *server);
	}
}

static void receive_and_display(struct data *server) {
	// receive data from server (first wait for ack)
	char resp[2];
	if (read(server->fd_from, resp, 2) == -1) {
		graceful_shutdown("fgets", *server);
	}
	if (resp[0] == 'e') {
		printf("This mailbox is empty!\n");
		fflush(stdout);
		return;
	} else if (resp[0] == 'f') {
		printf("Internal server error. please Try again later!\n");
		fflush(stdout);
		return;
	} else if (resp[0] != 'o') {
		printf("Server error. please Try again later!\n");
		fflush(stdout);
		return;
	}

	/* read all the mails from the shared region
	 * there are no newlines allowed in the message or user name
	 *
	 * Format:
	 *    Num_Emails
	 *    Email1-From/To
	 *    Email1-Message
	 *    Email2-From/To
	 *    Email2-Message
	 *    ...
	 *    Email<Num_Emails>-From/To
	 *    Email<Num_Emails>-Message
	 */
	int offset = 0;
	char num[12];
	read_line_from_mmap(server, 11, num, offset);
	offset += strlen(num) + 1; // don't forget the newline
	long num_mail = strtol(num, NULL, 10);
	if (num_mail <= 0 || num_mail >= INT_MAX) {
		printf("Internal error\n");
		fflush(stdout);
		graceful_shutdown(NULL, *server);
	}
	struct mail *mails = malloc(sizeof(struct mail) * num_mail);
	if (mails == NULL) {
		graceful_shutdown("malloc", *server);
	}
	for (int i = 0; i < num_mail; ++i) {
		char *from = malloc(sizeof(char) * 50);
		if (from == NULL) {
			graceful_shutdown("malloc", *server);
		}
		read_line_from_mmap(server, 49, from, offset);
		offset += strlen(from) + 1;

		char *message = malloc(sizeof(char) * 121);
		if (message == NULL) {
			graceful_shutdown("malloc", *server);
		}
		read_line_from_mmap(server, 120, message, offset);
		offset += strlen(message) + 1;
		mails[i].from = from;
		mails[i].message = message;
	}
	

	// ask for how many mails to display (->x)
	printf("How many mails (out of %ld) do you want to display?\n", num_mail);
	fflush(stdout);
	char line[12];
	if (!fgets(line, 12, stdin)) {
		graceful_shutdown("fgets", *server);
	}
	size_t len = strlen(line);
	if (len == 11 && line[10] != '\n') {
		printf("Number is too long!\n");
		fflush(stdout);
		int c;
		while ((c = fgetc(stdin))!= '\n' && c != EOF);
		if (c == EOF) {
			graceful_shutdown("fgetc", *server);
		}
	}
	long to_print  = strtol(line, NULL, 10);
	if (to_print == LONG_MIN || to_print >= INT_MAX) {
		graceful_shutdown("", *server);
	}
	if (to_print > 0) {
		if (to_print > num_mail) {
			printf("Cannot print %ld mails, will only print %ld.\n", to_print, num_mail);
			fflush(stdout);
			to_print = num_mail;
		}

		// display newest x mails
		for (int i = (int) num_mail - to_print; i < num_mail; ++i) {
			printf("%s: %s\n", mails[i].from, mails[i].message); // TODO do i want to sanatize names to not contain strange characters?
		}
		fflush(stdout);

		// dealloc resources
		for (int i = 0; i < num_mail; ++i) {
			free(mails[i].from);
			free(mails[i].message);
		}
		free(mails);
	}
}

static void view_inbox(struct data *server) {
	// generate and print random offset
	int offs = rand() % 256; // TODO we could increase this value
	printf("This request costs %d jigawatt!\n", offs);
	fflush(stdout);

	// write user and random offset to file
	//strcpy(server->mmap_region, str_offs);
	char *start_name = server->mmap_region + offs;
	strcpy(start_name, server->username);

	// send command to server
	signal_server(server, "vi\n", offs);

	receive_and_display(server);
	/* read all the mails from the shared region
	 * there are no newlines allowed in the message or user name
	 *
	 * Format:
	 *    Num_Emails
	 *    Email1-From
	 *    Email1-Message
	 *    Email2-From
	 *    Email2-Message
	 *    ...
	 *    Email<Num_Emails>-From
	 *    Email<Num_Emails>-Message
	 */
}

static void view_outbox(struct data *server) {
	// generate and print random offset
	int offs = rand() % 256;
	printf("This request costs %d jigawatt!\n", offs);
	fflush(stdout);

	// write user and random offset to file
	char *start_name = server->mmap_region + offs;
	strcpy(start_name, server->username);

	// send command to server
	signal_server(server, "vo\n", offs);

	// receive data from server and display
	receive_and_display(server);
}

static void write_mail(struct data *server) {
	// generate random offset and print
	int offs = rand() % 256;
	printf("This request costs %d jigawatt!\n", offs);

	// take recipient and body (limit to 120 chars)
	printf("This is the experimental version. Messages have a maximal\n");
	printf("length of 120 characters and cannot contain newlines.\n");
	printf("To: ");
	fflush(stdout);
	char to[50];
	if (!fgets(to, 50, stdin)) {
		graceful_shutdown("fgets", *server);
	}
	int len = strlen(to);
	if (len == 49 && to[48] != '\n') {
		printf("Recipient is too long!\n");
		fflush(stdout);
		int c;
		while ((c = fgetc(stdin))!= '\n' && c != EOF);
		if (c == EOF) {
			return;
		}
		return;
	}

	printf("Message: ");
	fflush(stdout);
	char message[121];
	if (!fgets(message, 121, stdin)) {
		graceful_shutdown("fgets", *server);
	}
	len = strlen(message);
	if (len == 120 && message[119] != '\n') {
		printf("Message is too long!\n");
		fflush(stdout);
		int c;
		while ((c = fgetc(stdin))!= '\n' && c != EOF);
		if (c == EOF) {
			return;
		}
		return;
	}

	// write stuff to mmap
	char *ptr = server->mmap_region + offs;
	strcpy(ptr, to);
	ptr += strlen(to) + 1;
	strcpy(ptr, message);

	// signal server
	signal_server(server, "s\n", offs);

	// wait for ack from server
	char resp[2];
	if (read(server->fd_from, resp, 2) == -1) {
		graceful_shutdown("fgets", *server);
	}
	if (resp[0] == 'o') {
		printf("Mail sent!\n");
	} else if (resp[0] == 'f') {
		printf("Internal server error. please Try again later!\n");
	} else if (resp[0] == 'u') {
		printf("Recipient unknown, could not send message!\n");
	} else {
		printf("Internal server error, please try again later\n");
	}
	fflush(stdout);
}
