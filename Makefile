CC     := gcc
CFLAGS := -Wall -Werror 

SRCS   := server.c  \
	mkfs.c \


OBJS   := ${SRCS:c=o}
PROGS  := ${SRCS:.c=}

compile: libmfs.so all

.PHONY: all
all: ${PROGS}

${PROGS} : % : %.o Makefile
	${CC} $< -o $@ udp.c ufs.h udp.h message.h

clean:
	rm -f ${PROGS} ${OBJS}
	rm -f libmfs.so libmfs.o

%.o: %.c Makefile
	${CC} ${CFLAGS} -c $<

libmfs.so: libmfs.o mkfs
	gcc -shared -Wl,-soname,libmfs.so -o libmfs.so libmfs.o udp.h udp.c -lc

libmfs.o: libmfs.c
	gcc -fPIC -g -c -Wall libmfs.c