CC = gcc
CFLAGS = -Wall -Wextra -g -pthread
LDLIBS = -pthread -lrt
TARGET = campuscompute
SRCS = main.c scheduler.c logger.c ipc.c banker.c worker.c limits.c all.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDLIBS)

%.o: %.c campuscompute.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
