#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>


#define BUFLEN 2056	// Tamanho do buffer
#define SIZE 128 

typedef struct User{
  char username[SIZE];
  char password[SIZE];
  int role;
} User;

char config_file[BUFLEN];
User users[200]; //! change this when i have patience to do it
int users_size = 0;
int s,recv_len, port_cofig, tcp_port;
pthread_t udp_server_t;
pthread_mutex_t users_mutex;

void error(char *s) {
	perror(s);
	exit(1);
}

void read_file(){
  FILE *fp = fopen(config_file, "r");

  if(fp == NULL){
    error("No configuration file found");
  }

  char line[BUFLEN], username[SIZE], password[SIZE], role[SIZE];
  while (fgets(line, 256, fp)){
    sscanf(line, "%[^;];%[^;];%[^\n]", username, password, role);
    if(strcmp(role, "administrador") == 0){
      users[users_size].role = 1;
    }else if(strcmp(role, "jornalista") == 0){
      users[users_size].role = 2;
    }else if(strcmp(role, "leitor") == 0){
      users[users_size].role = 3;
    }else{
      printf("Error wrong role\n");
      continue;
    }     
    strcpy(users[users_size].username, username);
    strcpy(users[users_size].password, password);
    users_size++;
  }
}

unsigned int login(){
  struct sockaddr_in si_outra;
  socklen_t slen = sizeof(si_outra);
  char buf[BUFLEN], cmd[BUFLEN], username[SIZE], password[SIZE];

   // User login
  while(1){
	  if((recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_outra, (socklen_t *)&slen)) == -1) {
	    error("Erro no recvfrom");
	  }

    // Ignores the buffer (anterior do buffer)
  	buf[recv_len]='\0';
  	if(buf[recv_len-1] == '\n')
  	  buf[recv_len-1] = '\0';

    if(recv_len < 2)
      continue;


    if(sscanf(buf, " %s", cmd) != 1){
      sprintf(buf, "ERROR: invalid format\n");
    }

    if(strcmp(cmd, "LOGIN") == 0){
      if(sscanf(buf, " %s %s %s", cmd, username, password) != 3)
        sprintf(buf, "ERROR: use format LOGIN {username} {password}\n");
      else{
        int i;
        for(i = 0; i < users_size; i++){
          if(strcmp(users[i].username, username) == 0 && strcmp(users[i].password, password) == 0 && users[i].role == 1){ // 1 means admin
            sprintf(buf, "USER LOGGED IN\n");
            break;
          }
        }
        if(i == users_size)
          sprintf(buf, "ERROR: invalid username or password\n");
      }
    }else{
      sprintf(buf, "ERROR: LOGIN before using commands\n");
    }
    if(sendto(s, buf, strlen(buf)+1, 0, (struct sockaddr *)&si_outra, slen) == -1){
      error("Erro no sendto");
    }
    if(strcmp(buf, "USER LOGGED IN\n") == 0)
      return ntohs(si_outra.sin_port);
  }
}

void *udp_server(){
  struct sockaddr_in si_minha, si_outra;
  socklen_t slen = sizeof(si_outra);
  char buf[BUFLEN], cmd[BUFLEN], username[SIZE], password[SIZE], role[SIZE];
  unsigned int current_user = 0;

  // Creates a socket to sent UDP packets
	if((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		error("Erro na criação do socket");
	}

  // Filling UDP socket
	si_minha.sin_family = AF_INET;
	si_minha.sin_port = htons(port_cofig);
	si_minha.sin_addr.s_addr = htonl(INADDR_ANY);

	// Bind the socket with the ip
	if(bind(s,(struct sockaddr*)&si_minha, sizeof(si_minha)) == -1) {
		error("Erro no bind");
	}
 
  while (strcmp(cmd, "QUIT_SERVER") != 0){
    memset(buf, '\0', BUFLEN);

	  // Wait to receive message
	  if((recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_outra, (socklen_t *)&slen)) == -1) {
	    error("Erro no recvfrom");
	  }

	  // Ignores the buffer (anterior do buffer)
  	buf[recv_len]='\0';
  	if(buf[recv_len-1] == '\n')
  	  buf[recv_len-1] = '\0';

    if(recv_len < 2)
      continue;
    
    if(sscanf(buf, " %s", cmd) != 1){
      sprintf(buf, "ERROR: invalid format\n");
    }
    
    if(current_user != ntohs(si_outra.sin_port)){ //!! change this first login command it not working
      sprintf(buf, "ERROR: LOGIN before using commands\n");
      if(sendto(s, buf, strlen(buf)+1, 0, (struct sockaddr *)&si_outra, slen) == -1){
        error("Erro no sendto");
      }
      current_user = login();
      continue;
    }

    if(strcmp(cmd, "LIST") == 0){
      pthread_mutex_lock(&users_mutex);
      sprintf(buf, "\nLISTING ALL USERS\nUSERNAME\tROLE\n");
      for(int i = 0; i < users_size; i++){
        strcat(buf, users[i].username);
        strcat(buf, users[i].role == 1 ? "\t\tadmin\n" : users[i].role == 2 ? "\t\tjournalist\n" : "\t\tclient\n");
      }
      pthread_mutex_unlock(&users_mutex);
    }else if(strcmp(cmd, "DEL") == 0){
      if(sscanf(buf, " %s %s", cmd, username) != 2)
        sprintf(buf, "ERROR: use format DEL {username}\n");
      else
        sprintf(buf, "Received order to delete user with name: %s\n", username); 
    }else if(strcmp(cmd,"ADD_USER") == 0){
      if(sscanf(buf, " %s %s %s %s", cmd, username, password, role) != 4)
        sprintf(buf, "ERROR: use format ADD_USER {username} {password} {admin|journalist|client}\n");
      else
        if(strcmp(role, "admin") == 0 || strcmp(role, "journalist") == 0 || strcmp(role, "client"))
          sprintf(buf, "Received order to user with username: %s and password: %s with role %sv", username, password, role); 
        else 
          sprintf(buf, "ERROR: invalid role\n");
    }else if(strcmp(cmd, "QUIT") == 0){
      sprintf(buf, "Received order quit\n");
      current_user = 0;
    }else if(strcmp(cmd, "QUIT_SERVER") == 0){
      sprintf(buf, "Closing server\n"); 
    }else{
      sprintf(buf, "ERROR: invalid command\n");
    }
    if(sendto(s, buf, strlen(buf)+1, 0, (struct sockaddr *)&si_outra, slen) == -1){
      error("Erro no sendto");
    }
  }

	// Closes the socket
	close(s);
  pthread_exit(NULL);
}

int main(int argc, char* argv[]) { 

  if(argc != 4)
    error("Error use format: ./news_server {tcp_port} {udp_port} {config file}");

  if(sscanf(argv[1], "%d", &tcp_port) != 1)
    error("Error invalid parameters");

  if(sscanf(argv[2], "%d", &port_cofig) != 1)
    error("Error invalid parameters");

  if(sscanf(argv[3], "%s", config_file) != 1)
    error("Error invalid parameters");

  // Read file
  read_file();

   /* Creates the mutex for the users array */
  if(pthread_mutex_init(&users_mutex, NULL) != 0){
    error("Not able to create mutex");
  }


  // UDP socket fork
  pthread_create(&udp_server_t, NULL, udp_server, NULL);

  pthread_join(udp_server_t, NULL);

	return 0;
}
