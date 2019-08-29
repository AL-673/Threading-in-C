TARGET=a.out
FLAGS=-std=c99 -Wall -g
INC=src
LIBS=-lpthread
SRC=src/*.c

all: clean
	gcc ${FLAGS} -I${INC} ${SRC} -o ${TARGET} ${LIBS}

clean:
	rm -rf ${TARGET}
