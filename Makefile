# build server

CC=g++
CFLAGS=-I. -Wall -g

.PHONY: clean all

all: server client

server: run_server.o server.o utils.o
	$(CC) -o server run_server.o server.o utils.o $(CFLAGS)

client: run_client.o client.o utils.o
	$(CC) -o client run_client.o client.o utils.o $(CFLAGS)

run_server.o: run_server.cc
	$(CC) -c run_server.cc $(CFLAGS)

run_client.o: run_client.cc
	$(CC) -c run_client.cc $(CFLAGS)

server.o: server.cc
	$(CC) -c server.cc $(CFLAGS)

client.o: client.cc
	$(CC) -c client.cc $(CFLAGS)

utils.o: utils.cc
	$(CC) -c utils.cc $(CFLAGS)

clean:
	rm *.o *.file server client core
