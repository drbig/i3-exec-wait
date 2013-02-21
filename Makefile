CFLAGS=-Wall -g
CC=gcc

all: i3-exec-wait

debug:
	$(CC) $(CFLAGS) -DDEBUG i3-exec-wait.c -o i3-exec-wait-dbg

clean:
	rm -f i3-exec-wait
