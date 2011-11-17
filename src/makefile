APP=reefs

# compilation flags
CC=gcc
C_FLAGS=-Wall -g
L_FLAGS=-lm -lrt -lpthread
HEADER=${APP}.h


# Rules

all:	${APP}

server.o: server.c ${HEADER}
	${CC} -c ${C_FLAGS} server.c -o server.o
session.o: session.c ${HEADER}
	${CC} -c ${C_FLAGS} session.c -o session.o
config.o: config.c ${HEADER}
	${CC} -c ${C_FLAGS} config.c -o config.o

main.o: main.c ${HEADER}
	${CC} -c ${C_FLAGS} main.c -o main.o
${APP}:	session.o server.o config.o main.o
	${CC} session.o server.o config.o main.o -o ${APP} ${L_FLAGS}


.PHONY:	test
test:
	./$(app)


.PHONY:	clean
clean:
	-rm -f server.o
	-rm -f session.o
	-rm -f config.o
	-rm -f main.o
	-rm -f ${APP}
