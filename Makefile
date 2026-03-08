TARGET_EXEC := WebServer WebClient

SRCS := WebServer.c WebClient.c
OBJS := WebServer.o WebClient.o

CC := gcc
OPTS := -Wall -O
LIBS := -lm -lpthread

all: $(TARGET_EXEC)

WebServer: WebServer.o
	$(CC) -o $@ $^ $(LIBS)

WebClient: WebClient.o
	$(CC) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(OPTS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET_EXEC)
