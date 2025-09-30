CPPFLAGS = -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Os ${CPPFLAGS}
LDFLAGS = -lpthread
CC = cc

SRC = spacebard.c ctrld.c
OUT = ${SRC:.c=}

all: ${OUT}

.c:
	${CC} -o $@ ${CFLAGS} ${LDFLAGS} $<

${OUT}: Makefile

clean:
	rm -f ${OUT}

.PHONY: all clean
