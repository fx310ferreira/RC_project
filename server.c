#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <semaphore.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <signal.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/wait.h>


// ! QUIT SERVER WHEN WRITING QUIT_SERVER BEFORE LOGIN
#define BUFLEN 2056	// Tamanho do buffer
#define SIZE 128 
#define MAX_USERS 4
typedef struct User{
  char username[SIZE];
  char password[SIZE];
  int role;
} User;

char config_file[BUFLEN];
User* users;
int* n_childs;
int shmid;
int users_size = 0;
int udp_socket,recv_len, port_cofig, tcp_port, udp_thread = 0;
struct sockaddr_in si_upd_client;
pthread_t udp_server_t;
sem_t *users_sem, *n_childs_sem;

void save_file(){
  FILE *fp = fopen(config_file, "w");

  int i;
  for(i = 0; i < users_size; i++){
    fprintf(fp, "%s;%s;", users[i].username, users[i].password);
    if(users[i].role == 1){
      fprintf(fp, "administrador\n");
    }else if(users[i].role == 2){
      fprintf(fp, "jornalista\n");
    }else if(users[i].role == 3){
      fprintf(fp, "leitor\n");
    }else{
      printf("Error wrong role\n");
      continue;
    }     
  }
}

void cleanup(){
  printf("Closing server\n");
  if(udp_thread){
    pthread_cancel(udp_server_t);
  }
  pthread_join(udp_server_t, NULL);
  for (int i = 0; i < *n_childs; i++){
    wait(NULL);
  }
  
  //saves data to the file
  save_file();
  //closing the udp socket
  close(udp_socket);
  sem_close(users_sem);
  sem_unlink("users_sem");
  shmdt(users);
  shmctl(shmid, IPC_RMID, NULL);
  exit(0);
}

void error(char *s) {
	perror(s);
	cleanup();
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

unsigned int login(char * buffer){
  socklen_t slen = sizeof(si_upd_client);
  char buf[BUFLEN], cmd[BUFLEN], username[SIZE], password[SIZE];
  strcpy(buf, buffer);
   // User login
  while(1){


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

    if(sendto(udp_socket, buf, strlen(buf)+1, 0, (struct sockaddr *)&si_upd_client, slen) == -1){
      error("Erro no sendto");
    }

    if(strcmp(buf, "USER LOGGED IN\n") == 0)
      return ntohs(si_upd_client.sin_port);
    else if((recv_len = recvfrom(udp_socket, buf, BUFLEN, 0, (struct sockaddr *) &si_upd_client, (socklen_t *)&slen)) == -1) {
	    error("Erro no recvfrom");

      // Ignores the buffer (anterior do buffer)
      buf[recv_len]='\0';
      if(buf[recv_len-1] == '\n')
        buf[recv_len-1] = '\0';

      if(recv_len < 2)
        continue;
	  }
  }
}

void tcp_client(int client_fd){
  sem_wait(users_sem);
  *n_childs = *n_childs+1;
  sem_post(users_sem);
	int nread = 0;
  // Creates 2 strings for future use
	char buffer[1024], msg[1246];
  // Send the message to the conected client
  sprintf(msg, "Bem-vindo ao servidor de nomes do DEI. Indique o nome de domínio");
  write(client_fd, msg, 1024);
  do {
    // Reads the domain sent by the client
    nread = read(client_fd, buffer, 1024);
    // If a string is received continues
    if(nread > 0){
      // If the received string equals "SAIR" sends the message "Ate logo!" to client and closes the connection
      if(strcmp(buffer, "SAIR") != 0){
        int found = 0;
        // Searches the file for the domain received
        // If no equal domain is found in the file the server sends the message bellow
        if(!found){
          sprintf(msg, "O nome de domínio %s não tem um endereço IP associado", buffer);
        }
        write(client_fd, msg, 1024);
      }else{
        // If the message sent by the client is equal to "SAIR" the server responds with the message "Ate logo!" 
        sprintf(msg, "Até logo!");
        write(client_fd, msg, 1024);
        break;
      }
    }
	  fflush(stdout);
  } while (1);
	close(client_fd);
}

void *udp_server(){
  struct sockaddr_in si_udp;
  socklen_t slen = sizeof(si_upd_client);
  char buf[BUFLEN], cmd[BUFLEN], username[SIZE], password[SIZE], role[SIZE];
  unsigned int current_user = 0;
  int new_user = 1;

  udp_thread = 1;

  // Creates a socket to sent UDP packets
	if((udp_socket=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		error("Erro na criação do socket");
	}

  // Filling UDP socket
	si_udp.sin_family = AF_INET;
	si_udp.sin_port = htons(port_cofig);
	si_udp.sin_addr.s_addr = htonl(INADDR_ANY);

	// Bind the socket with the ip
	if(bind(udp_socket,(struct sockaddr*)&si_udp, sizeof(si_udp)) == -1) {
		error("Erro no bind");
	}
 
  while (strcmp(cmd, "QUIT_SERVER") != 0){
    memset(buf, '\0', BUFLEN);

	  // Wait to receive message
	  if((recv_len = recvfrom(udp_socket, buf, BUFLEN, 0, (struct sockaddr *) &si_upd_client, (socklen_t *)&slen)) == -1) {
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
    
    if(current_user != ntohs(si_upd_client.sin_port)){ //!! change this first login command it not working
      current_user = login(buf);
      continue;
    }

    if(strcmp(cmd, "LIST") == 0){
      sprintf(buf, "\nLISTING ALL USERS\nUSERNAME\tROLE\n");
      sem_wait(users_sem);
      for(int i = 0; i < users_size; i++){
        strcat(buf, users[i].username);
        strcat(buf, users[i].role == 1 ? "\t\tadmin\n" : users[i].role == 2 ? "\t\tjournalist\n" : "\t\tclient\n");
      }
      sem_post(users_sem);
    }else if(strcmp(cmd, "DEL") == 0){
      if(sscanf(buf, " %s %s", cmd, username) != 2)
        sprintf(buf, "ERROR: use format DEL {username}\n");
      else{
        sem_wait(users_sem);
        sprintf(buf, "User %s not found\n", username); 
        for(int i = 0; i < users_size; i++){
          if(strcmp(users[i].username, username) == 0){
            users[i] = users[users_size-1];
            users_size--;
            sprintf(buf, "User %s deleted\n", username);
            break;
          }
        }
        sem_post(users_sem);
      }
    }else if(strcmp(cmd,"ADD_USER") == 0){
      new_user = 1;
      if(sscanf(buf, " %s %s %s %s", cmd, username, password, role) != 4)
        sprintf(buf, "ERROR: use format ADD_USER {username} {password} {admin|journalist|client}\n");
      else
        if(strcmp(role, "admin") == 0 || strcmp(role, "journalist") == 0 || strcmp(role, "client")){
          sem_wait(users_sem);
          if(users_size < MAX_USERS){
            for(int i = 0; i < users_size; i++){
              if(strcmp(users[i].username, username) == 0){
                sprintf(buf, "ERROR: username already exists\n");
                new_user = 0;
                break;
              }
            }
            if(new_user){
              strcpy(users[users_size].username, username);
              strcpy(users[users_size].password, password);
              users[users_size].role = strcmp(role, "admin") == 0 ? 1 : strcmp(role, "journalist") == 0 ? 2 : 3;
              users_size++;
              sprintf(buf, "User %s added with role %s\n", username, role);
            }
          }else
            sprintf(buf, "ERROR: max number of users reached\n");
          sem_post(users_sem);
        }
        else 
          sprintf(buf, "ERROR: invalid role\n");
    }else if(strcmp(cmd, "QUIT") == 0){
      sprintf(buf, "Received order quit\n");
      current_user = 0;
    }else if(strcmp(cmd, "QUIT_SERVER") == 0){
      sprintf(buf, "Quiting server\n"); 
    }else{
      sprintf(buf, "ERROR: invalid command\n");
    }
    if(sendto(udp_socket, buf, strlen(buf)+1, 0, (struct sockaddr *)&si_upd_client, slen) == -1){
      error("Erro no sendto");
    }
  }
  udp_thread = 0;
  pthread_exit(NULL);
}

void tcp_server(){
  int fd, client;
  struct sockaddr_in addr, client_addr;
  int client_addr_size;

  bzero((void *) &addr, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(tcp_port);

  if ( (fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	  error("na funcao socket");
  if ( bind(fd,(struct sockaddr*)&addr,sizeof(addr)) < 0)
	  error("na funcao bind");
  if( listen(fd, 5) < 0)
	  error("na funcao listen");

  client_addr_size = sizeof(client_addr);

  while (1) {
    //wait for new connection
    client = accept(fd,(struct sockaddr *)&client_addr,(socklen_t *)&client_addr_size);
    
    if (client > 0) {
      sem_wait(n_childs_sem);
      *n_childs = *n_childs + 1;
      sem_post(n_childs_sem);
      if (fork() == 0) {
        close(fd);
        tcp_client(client);
        exit(0);
      }
    close(client);
    }
  }
}

void syc_creator(){
   /* Creates the semaphore for the users array */
  if((users_sem = sem_open("users_sem", O_CREAT | O_EXCL, 0700, 1)) == SEM_FAILED){
      if((users_sem = sem_open("users_sem", 0)) == SEM_FAILED){
        error("Error opening semaphore");
      }
  } 

  if((n_childs_sem = sem_open("n_childs_sem", O_CREAT | O_EXCL, 0700, 1)) == SEM_FAILED){
      if((n_childs_sem = sem_open("n_childs_sem", 0)) == SEM_FAILED){
        error("Error opening semaphore");
      }
  }

  if((shmid = shmget(IPC_PRIVATE, sizeof(User)*MAX_USERS + sizeof(int), IPC_CREAT | 0777)) == -1){
    error("Error creating shared memory");
  }
  

  if((users = (User*)shmat(shmid, NULL, 0)) == (void*)-1){
    error("Error attaching shared memory");
  }

  n_childs = (int*)(((void*) users) + MAX_USERS*sizeof(User));
  *n_childs = 0;
}

void ctrlc_handler(){
    printf("\nSIGINT recived\n");
    cleanup();
}

int main(int argc, char* argv[]) { 

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

  if(argc != 4)
    error("Error use format: ./news_server {tcp_port} {udp_port} {config file}");

  if(sscanf(argv[1], "%d", &tcp_port) != 1)
    error("Error invalid parameters");

  if(sscanf(argv[2], "%d", &port_cofig) != 1)
    error("Error invalid parameters");

  if(sscanf(argv[3], "%s", config_file) != 1)
    error("Error invalid parameters");

  // Creates the sincroniztion mechanisms
  syc_creator();

  // Read file
  read_file();

  // UDP socket fork
  pthread_create(&udp_server_t, NULL, udp_server, NULL);

  tcp_server();

  pthread_join(udp_server_t, NULL);
   
  cleanup();

	return 0;
}
