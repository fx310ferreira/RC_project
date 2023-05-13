#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

void erro(char *msg);

int main(int argc, char *argv[]) {
  char endServer[100];
  int fd, nread;
  struct sockaddr_in addr;
  struct hostent *hostPtr;

  if (argc != 3) {
    printf("cliente <host> <port> \n");
    exit(-1);
  }

  strcpy(endServer, argv[1]);
  if ((hostPtr = gethostbyname(endServer)) == 0)
    erro("Não consegui obter endereço");

  bzero((void *) &addr, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ((struct in_addr *)(hostPtr->h_addr))->s_addr;
  addr.sin_port = htons((short) atoi(argv[2]));

  if ((fd = socket(AF_INET,SOCK_STREAM,0)) == -1)
	  erro("socket");
  if (connect(fd,(struct sockaddr *)&addr,sizeof (addr)) < 0)
	  erro("Connect");

// Create the arrays to store the response and the domain 
  char msg[1024], domain[1024];
// Waits for the first message sent by the server  
  nread = read(fd, msg, 1024);
// If a message is received the client prints the message
  if (nread > 0){
    printf("%s\n", msg);
  }
  do{
    // Reads the domain from the console
    scanf("%s", domain);
    // Sends the domain to the server
    write(fd, domain, 1024);
    // Waits for the server response to the given domain
    read(fd, msg, 1024);
    // Prints the response from the server
    printf("%s\n", msg);
  }while (strcmp(msg, "Até logo!") != 0); // If the server response is the string "Ate logo!" the connection is closed
  close(fd);
  exit(0);
}

void erro(char *msg) {
  printf("Erro: %s\n", msg);
	exit(-1);
}
