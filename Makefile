CC=g++

all: server client

server: server.o
	$(CC) -o server server.o -lpthread

server.o: server.cpp
	$(CC) -c server.cpp -lpthread

client: client.o
	$(CC) -o client client.o -lpthread

client.o: client.cpp
	$(CC) -c client.cpp -lpthread

.PHONY: clean all

clean:
	rm -f *.o *.a *.out server client