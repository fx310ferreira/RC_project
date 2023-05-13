all: news_server client

news_server: server.c
	gcc server.c -Wall -Wextra -pthread -g -o news_server

client: tcp_client.c
	gcc tcp_client.c -Wall -Wextra -pthread -g -o client
