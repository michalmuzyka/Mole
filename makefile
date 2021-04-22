CC=gcc
CFLAGS= -std=gnu99 -Wall
LDLIBS= -lpthread -lm

.PHONY: all clean

all: mole

mole: main.c index.h index.c commands.h commands.c defines.h
	${CC} ${CFLAGS} -o mole main.c index.c commands.c ${LDLIBS}

clean:
	rm mole