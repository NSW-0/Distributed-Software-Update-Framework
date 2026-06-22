CC      = gcc
CFLAGS  = -Wall -Wextra -g -pthread
LDFLAGS_SERVER = -pthread -lGL -lGLU -lglut -lm
LDFLAGS_CLIENT = -pthread

SERVER_SRCS = common.c server.c config.c logger.c display.c
CLIENT_SRCS = common.c client.c config.c logger.c
SERVER_OBJS = $(SERVER_SRCS:.c=_s.o)
CLIENT_OBJS = $(CLIENT_SRCS:.c=_c.o)

.PHONY: all clean run-server run-client install-deps

all: server client

server: $(SERVER_OBJS)
	$(CC) $(SERVER_OBJS) $(LDFLAGS_SERVER) -o server

client: $(CLIENT_OBJS)
	$(CC) $(CLIENT_OBJS) $(LDFLAGS_CLIENT) -o client

%_s.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%_c.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run-server: server
	./server server_config.txt

run-client: client
	./client client_config.txt

install-deps:
	sudo apt-get install -y gcc make freeglut3-dev

clean:
	rm -f *_s.o *_c.o server client server.log client.log
	rm -f update_package.bin
	rm -rf downloads/
