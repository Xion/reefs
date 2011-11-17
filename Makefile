APP=reefs

# compilation flags
CC=gcc
C_FLAGS=-Wall -g
L_FLAGS=-lm -lrt -lpthread
HEADER=${APP}.h


# Rules

all:	${APP}

server.o: src/server.c src/${HEADER}
	${CC} -c ${C_FLAGS} src/server.c -o obj/server.o
session.o: src/session.c src/${HEADER}
	${CC} -c ${C_FLAGS} src/session.c -o obj/session.o
config.o: src/config.c src/${HEADER}
	${CC} -c ${C_FLAGS} src/config.c -o obj/config.o

main.o: src/main.c src/${HEADER}
	${CC} -c ${C_FLAGS} src/main.c -o obj/main.o
${APP}:	session.o server.o config.o main.o
	${CC} obj/session.o obj/server.o obj/config.o obj/main.o -o bin/${APP} ${L_FLAGS}


.PHONY:	test
test:
	./bin/${APP}


.PHONY:	clean
clean:
	rm -rf ./bin/${APP}
	rm -rf ./obj/*.o
