#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>

void erro(char *msg) {
  printf("Erro: %s\n", msg);
	exit(-1);
}

int leave = 0;
int fd, role = 0;
pthread_t multicast_thread_t;

void ctrlc_handler(){
  leave = 1;
}

void *multicast_thread(){
  struct sockaddr_in addr;
  socklen_t slen = sizeof(addr);
  int multicast_socket;

  if ((multicast_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket");
    exit(1);
  }
  // set up the multicast address structure
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(1025);

  // set the SO_REUSEADDR option
  int optval = 1;
  if (setsockopt(multicast_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
      perror("setsockopt");
      exit(1);
  }


  char msg[1024];
  // bind the socket to the port
  if (bind(multicast_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("multicast bind");
    exit(1);
  }

  // join the multicast group
  struct ip_mreq mreq;
  mreq.imr_multiaddr.s_addr = inet_addr("239.0.0.1");
  mreq.imr_interface.s_addr = INADDR_ANY;
  if (setsockopt(multicast_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
    perror("multicast setsockopt");
    exit(1);
  }

  int nbytes;
  if ((nbytes = recvfrom(multicast_socket, msg, sizeof(msg), 0, (struct sockaddr *)&addr, &slen)) < 0) {
    perror("multicast recvfrom");
    exit(1);
  }
printf("Received multicast message: %s\n", msg);

// close the socket
close(multicast_socket);

  pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
  char endServer[100];
  int  nread, temp;
  struct sockaddr_in addr;
  struct hostent *hostPtr;


  if (argc != 3) {
    printf("cliente <host> <port> \n");
    exit(-1);
  }

  // Initialize the signal handler
  struct sigaction ctrlc;
  ctrlc.sa_handler = ctrlc_handler;
  sigfillset(&ctrlc.sa_mask);
  ctrlc.sa_flags = 0;
  sigaction(SIGINT, &ctrlc, NULL);

  sigset_t mask;
  sigfillset(&mask);
  sigdelset(&mask, SIGINT);
  sigprocmask(SIG_SETMASK, &mask, NULL);

  pthread_create(&multicast_thread_t, NULL, multicast_thread, NULL);


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
  char msg[1024], aux[1024], cmd[1024], username[1024], password[1024];
// Waits for the first message sent by the server  
  nread = read(fd, msg, 1024);
// If a message is received the client prints the message
  if (nread > 0){
    printf("%s\n", msg);
  }
  do{
    // Reads the command from the user
    memset(cmd, 0, 1024);
    fgets(msg, 1024, stdin);
    if(sscanf(msg, " %s", cmd) != 1 || strlen(cmd) < 2){
      continue;
    }
    if(!strcmp(cmd, "LOGIN")){
      if(role){
        printf("You are already logged in\n");
        continue;
      }
      if(sscanf(msg, " %s %s %s", cmd, username, password) != 3){
        printf("LOGIN {username} {password}\n");
        continue;
      }
      // Sends the domain to the server
      write(fd, msg, 1024);
      // Waits for the server response to the given domain
      read(fd, msg, 1024);
      // Prints the response from the server
      if(sscanf(msg, " %d", &role) != 1){
        printf("Invalid command\n");
        continue;
      }else if(role == 0){
        printf("Invalid username or password\n");
        continue;
      }else if (role == 1){
        printf("Reader logged in\n");
        continue;
      }else if (role == 2){
        printf("Journalist logged in\n");
        continue;
      }
    }else if(!strcmp(cmd, "LIST_TOPICS") && role == 1){
      // Sends the domain to the server
      write(fd, msg, 1024);
      // Waits for the server response to the given domain
      read(fd, msg, 1024);
      // Prints the response from the server
      printf("%s\n", msg);
    }else if(!strcmp(cmd, "SUBSCRIBE_TOPIC") && role == 1){
      if(sscanf(msg, " %s %s", cmd, aux) != 2){
        printf("SUBSCRIBE_TOPIC {topic}\n");
        continue;
      }
      write(fd, msg, 1024);
      read(fd, msg, 1024);
      if(sscanf(msg, " %d", &temp) != 1){
        printf("Invalid command\n");
        continue;
      }
      if(temp == 0){
        printf("Invalid topic\n");
        continue;
      }else{
        printf("You are now subscribed in this topic with multicast port: %d\n", temp);
        continue;
      }
    }else if(!strcmp(cmd, "SEND_NEWS") && role == 2){
      if(sscanf(msg, " %s %s %s", cmd, aux, aux) != 3){
        printf("SEND_NEWS {topic} {news}\n");
        continue;
      }
      write(fd, msg, 1024);
      read(fd, msg, 1024);
      if(sscanf(msg, " %d", &temp) != 1){
        printf("Invalid command\n");
        continue;
      }
      if(temp == 0){
        printf("Invalid topic\n");
        continue;
      }else if(temp == 1){
        printf("News sent\n");
        continue;
      }
    }else if(!strcmp(cmd, "CREATE_TOPIC") && role == 2){
      if(sscanf(msg, " %s %s %s", cmd, username, password) != 3){
        printf("CREATE_TOPIC {id} {topic}\n");
        continue;
      }
      write(fd, msg, 1024);
      read(fd, msg, 1024);
      if(sscanf(msg, " %d", &temp) != 1){
        printf("Invalid command\n");
        continue;
      }
      if(temp == 0){
        printf("Failed to create topic\n");
        continue;
      }else if(temp == 1){
        printf("Topic created\n");
        continue;
      }
    }else if(role == 0){
      printf("You need to login first\n");
      continue;
    }else{
      printf("Invalid command\n");
      continue;
    }
  
  pthread_join(multicast_thread_t, NULL);

  }while (!leave); // If the server response is the string "Ate logo!" the connection is closed
  printf("\nConnection closed\n");
  close(fd);
  exit(0);
}
