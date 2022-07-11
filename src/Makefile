CFLAGS = -std=c11 -pedantic -Wall -Werror -Wextra -D_FORTIFY_SOURCE=2 -O3 -fPIC -fstack-protector-all -pie -s
CC=gcc

.PHONY: all

all: fluxmail

fluxmail.o: fluxmail.c include/client.h include/server.h include/util.h include/flux-types.h
	$(CC) $(CFLAGS) -c -o $@ $<

server.o: server.c include/flux-types.h include/server.h
	$(CC) $(CFLAGS) -c -o $@ $<

client.o: client.c include/client.h include/util.h include/flux-types.h
	$(CC) $(CFLAGS) -c -o $@ $<

util.o: util.c include/util.h
	$(CC) $(CFLAGS) -c -o $@ $<

fluxmail: fluxmail.o server.o client.o util.o
	$(CC) $(CFLAGS) -o $@ $^
