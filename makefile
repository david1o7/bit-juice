CC = gcc
CFLAGS = -Wall -Wextra -g -pthread
LDFLAGS = -pthread

SRCS = main.c server.c http.c
OBJS = $(SRCS:.c=.o)
TARGET = http_server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean