CC     := gcc
CFLAGS := -Wall -Werror 

SRCS   := server.c  \
	mkfs.c \


OBJS   := ${SRCS:c=o}
PROGS  := ${SRCS:.c=}

compile: mfs.so all

.PHONY: all
all: ${PROGS}

${PROGS} : % : %.o Makefile
	${CC} $< -o $@ udp.c ufs.h udp.h message.h

clean:
	rm -f ${PROGS} ${OBJS}
	rm -f mfs.so mfs.o

%.o: %.c Makefile
	${CC} ${CFLAGS} -c $<

mfs.so: mfs.o mkfs
	gcc -shared -Wl,-soname,mfs.so -o mfs.so mfs.o udp.h udp.c -lc

mfs.o: mfs.c
	gcc -fPIC -g -c -Wall mfs.c

main:
	gcc -o main main.c -Wall -L. -lmfs
	
runTests:
	sh /home/cs537-1/tests/p4/p4-test-12.11/runtests.sh -c

makeImage: mkfs
	./mkfs -f test.img