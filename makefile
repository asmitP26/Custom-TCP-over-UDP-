CC = gcc
CFLAGS = -Wall -g

all: libksocket.a initksocket user1 user2

libksocket.a: ksocket.o
	ar rcs libksocket.a ksocket.o

ksocket.o: ksocket.c ksocket.h
	$(CC) $(CFLAGS) -c ksocket.c -o ksocket.o

initksocket: initksocket.c ksocket.h libksocket.a
	$(CC) $(CFLAGS) -o initksocket initksocket.c -L. -lksocket -lpthread

user1: user1.c ksocket.h libksocket.a
	$(CC) $(CFLAGS) -o user1 user1.c -L. -lksocket

user2: user2.c ksocket.h libksocket.a
	$(CC) $(CFLAGS) -o user2 user2.c -L. -lksocket

clean:
	rm -f *.o *.a initksocket user1 user2 output.txt
