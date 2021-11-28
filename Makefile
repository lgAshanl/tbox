CCFLAGS=$(CFLAGS) -std=c++14 -pipe `pkg-config --cflags gtk+-3.0`
CLDFLAGS=$(LDFLAGS) `pkg-config --libs gtk+-3.0`

SCFLAGS+=$(CFLAGS) -std=c++14 -pipe -pthread

#-fsanitize=thread
#address

CLIENTSRC=./src/client.cpp ./src/tb_clientapp.cpp ./src/tbnetqueue.cpp
#CLIENTSRC=client.cpp tb_clientapp.cpp tbnetqueue.cpp tbserver.cpp
SERVERSRC=./src/server.cpp ./src/tbserver.cpp ./src/tbnetqueue.cpp

all: build
	
build: client server

client:
	g++ -o client.out $(CLIENTSRC) $(CCFLAGS) $(CLDFLAGS)

server:
	g++ -o server.out $(SERVERSRC) $(SCFLAGS) $(LDFLAGS)

clean:
	rm client.out
	rm server.out
