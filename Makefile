CFLAGS=-Wall -g -lyajl
CC=gcc

all: i3-exec-wait

hacks:
	$(CC) $(CFLAGS) -lxcb -lxcb-ewmh -DDEBUG i3-exec-wait-hacks.c -o i3-exec-wait-hacks

debug:
	$(CC) $(CFLAGS) -DDEBUG i3-exec-wait.c -o i3-exec-wait-dbg

clean:
	rm -f i3-exec-wait i3-exec-wait-dbg i3-exec-wait-hacks
