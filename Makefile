CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
OBJS = note.o fb.o

cNote: $(OBJS)
	$(CC) $(CFLAGS) -o cNote $(OBJS)

clean:
	rm -f cNote $(OBJS)
