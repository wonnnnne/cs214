CC		= gcc
CFLAGS 	= -W -Wall

all: clean bankingServer bankingClient

bankingServer: 
	$(CC) server.c -o $@ -pthread

bankingClient:
	$(CC) client.c -o $@ -pthread

clean:
	rm -f bankingServer bankingClient core
