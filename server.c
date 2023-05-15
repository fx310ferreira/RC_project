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

#define BUFLEN 2056	// Tamanho do buffer
#define SIZE 128 
#define BUF_SIZE 1024
#define MAX_USERS 100
#define MAX_WORKERS 100
#define MAX_TOPICS 100
typedef struct User{
  char username[SIZE];
  char password[SIZE];
  int role;
  int ports[MAX_TOPICS];
  int n_topics;
} User;

typedef struct Topic{
  char name[SIZE];
  char id[SIZE];
  struct sockaddr_in addr;
  int multicast_socket;
  }Topic;

typedef struct Shm{
  User* users;
  Topic* topics;
  int n_childs;
  int worker_pid[MAX_WORKERS];
  int users_size;
  int topics_size;
  int port;
}Shm;

char config_file[BUFLEN];
int shmid;
Shm *shm;
int count = 0;
int udp_socket,recv_len, port_cofig, tcp_port, udp_thread = 0, tcp_client_n, parent_pid, tcp_socket, leave = 0;
struct sockaddr_in si_upd_client;
pthread_t udp_server_t;
sem_t *users_sem, *n_childs_sem, *topics_sem;

void save_file(){
  FILE *fp = fopen(config_file, "w");

  int i;
  for(i = 0; i < shm->users_size; i++){
    fprintf(fp, "%s;%s;", shm->users[i].username, shm->users[i].password);
    if(shm->users[i].role == 1){
      fprintf(fp, "administrador\n");
    }else if(shm->users[i].role == 2){
      fprintf(fp, "jornalista\n");
    }else if(shm->users[i].role == 3){
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
    pthread_join(udp_server_t, NULL);
  }

  printf("Wating for workers to exit\n");
  if( shm != NULL){
    for (int i = 0; i < shm->n_childs; i++){
      wait(NULL);
    }
    for (int i = 0; i < shm->topics_size; i++){
      close(shm->topics[i].multicast_socket);
    }
  }
  
  //saves data to the file
  // save_file();
  //closing the udp socket
  close(udp_socket);
  if(close(tcp_socket) < 0){
    printf("Error closing tcp socket\n");
  }
  sem_close(users_sem);
  sem_unlink("users_sem");
  sem_close(topics_sem);
  sem_unlink("topics_sem");
  sem_close(n_childs_sem);
  sem_unlink("n_childs_sem");
  shmdt(shm);
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
      shm->users[shm->users_size].role = 1;
    }else if(strcmp(role, "jornalista") == 0){
      shm->users[shm->users_size].role = 2;
    }else if(strcmp(role, "leitor") == 0){
      shm->users[shm->users_size].role = 3;
    }else{
      printf("Error wrong role\n");
      continue;
    }     
    strcpy(shm->users[shm->users_size].username, username);
    strcpy(shm->users[shm->users_size].password, password);
    shm->users_size++;
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
        for(i = 0; i < shm->users_size; i++){
          if(strcmp(shm->users[i].username, username) == 0 && strcmp(shm->users[i].password, password) == 0 && shm->users[i].role == 1){ // 1 means admin
            sprintf(buf, "USER LOGGED IN\n");
            break;
          }
        }
        if(i == shm->users_size)
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

void create_topic(char* id, char* name){
  
  printf("CREATING SOCKET to %d %d\n",shm->topics_size, shm->topics[shm->topics_size].multicast_socket);
  if ((shm->topics[shm->topics_size].multicast_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket");
    exit(1);
  }
  printf("SOCKET created to %d %d\n",shm->topics_size, shm->topics[shm->topics_size].multicast_socket);

  // set up the multicast address structure
  memset(&shm->topics[shm->topics_size].addr, 0, sizeof(shm->topics[shm->topics_size].addr));
  shm->topics[shm->topics_size].addr.sin_family = AF_INET;
  shm->topics[shm->topics_size].addr.sin_addr.s_addr = inet_addr("239.0.0.1");
  shm->topics[shm->topics_size].addr.sin_port = htons(shm->port);

  // enable multicast on the socket
  int enable = 10;
  if (setsockopt(shm->topics[shm->topics_size].multicast_socket, IPPROTO_IP, IP_MULTICAST_TTL, &enable, sizeof(enable)) < 0) {
    perror("setsockopt");
    exit(1);
  }

  strcpy(shm->topics[shm->topics_size].name, name);
  strcpy(shm->topics[shm->topics_size].id, id);
  shm->topics_size++;
  shm->port++;

}

int send_news(char* id, char* msg){
  for (int i = 0; i < shm->topics_size; i++){
    if(strcmp(shm->topics[i].id, id) == 0){
      // send the multicast message
      if (sendto(shm->topics[i].multicast_socket, msg, BUF_SIZE, 0, (struct sockaddr *)&shm->topics[i].addr, sizeof(shm->topics[i].addr)) < 0) {
        perror("sendto");
        exit(1);
      }
      return 1;
    }
  }
  return 0;
}

void tcp_client(){  
	int nread, new_topic;
  // Creates 2 strings for future use
	char buffer[BUF_SIZE], msg[1246], cmd[BUF_SIZE], username[BUF_SIZE], password[BUF_SIZE];
  // Send the message to the conected client
  sprintf(msg, "Welcome to the news server, login usign: LOGIN {username} {password}");
  write(tcp_client_n, msg, BUF_SIZE);
  do {
    // Leave if ctrl+c is pressed
    if(leave){
      printf("\nClosing client\n");
      close(tcp_client_n);
      return;
    }

    // Reads the domain sent by the client
    if((nread = read(tcp_client_n, buffer, BUF_SIZE)) <= 0) continue;
    if(sscanf(buffer, " %s", cmd) != 1) continue;;

    if(strcmp(cmd, "LOGIN") == 0){
      if(sscanf(buffer, " %s %s %s", cmd, username, password) != 3) continue;
      sprintf(msg, "0");
      sem_wait(users_sem);
      for(int i = 0; i < shm->users_size ; i++){
        if(strcmp(shm->users[i].username, username) == 0 && strcmp(shm->users[i].password, password) == 0){
          if(shm->users[i].role == 2){
            sprintf(msg, "1"); //journalist
          }else if(shm->users[i].role == 3){
            sprintf(msg, "2 "); //client
            for(int j = 0; j < shm->users[i].n_topics; j++){
              sprintf(username, "%d ", shm->users[i].ports[j]);
              strcat(msg, username);
            }
          }
        }
      }
      sem_post(users_sem);
      write(tcp_client_n, msg, BUF_SIZE);
    }else if(strcmp(cmd, "LIST_TOPICS") == 0){
      sprintf(msg, "ID\tTOPICS\n");
      sem_wait(topics_sem);
      for(int i = 0; i < shm->topics_size; i++){
        sprintf(username ,"%s\t%s\n", shm->topics[i].id, shm->topics[i].name);
        strcat(msg, username);
      }
      sem_post(topics_sem);
      write(tcp_client_n, msg, BUF_SIZE);
    }else if(strcmp(cmd, "SUBSCRIBE_TOPIC") == 0){
      if(sscanf(buffer, " %s %s %s", cmd, password, username) != 3) continue;
      sprintf(msg, "0");
      sem_wait(topics_sem);
      for(int i = 0; i < shm->topics_size; i++){
        if(strcmp(shm->topics[i].id, password) == 0){
          sem_wait(users_sem);
          for (int j = 0; j < shm->users_size; j++){
            if(strcmp(shm->users[j].username, username) == 0){
              shm->users[j].ports[shm->users[j].n_topics] = htons(shm->topics[i].addr.sin_port);
              shm->users[j].n_topics++;
            }
          }
          sem_post(users_sem);
          sprintf(msg, "%d", htons(shm->topics[i].addr.sin_port));
        }
      }      
      sem_post(topics_sem);
      write(tcp_client_n, msg, BUF_SIZE);
    }else if (strcmp(cmd, "CREATE_TOPIC") == 0){  
      if(sscanf(buffer, " %s %s %s", cmd, username, password) != 3) continue;
      sprintf(msg, "1");
      new_topic = 1;
      sem_wait(topics_sem);
      for(int i = 0; i < shm->topics_size; i++){
        if(strcmp(shm->topics[i].id, username) == 0){
          sprintf(msg, "0");
          new_topic = 0;
        }
      }
      if(new_topic){
        create_topic(username, password);
      }
      sem_post(topics_sem);
      write(tcp_client_n, msg, BUF_SIZE);
    }else if (strcmp(cmd, "SEND_NEWS") == 0){
      if(sscanf(buffer, " %s %s %s", cmd, username, password) != 3) continue;
      sem_wait(topics_sem);
      if(send_news(username, password))
        sprintf(msg, "1");
      else
        sprintf(msg, "0");
      sem_post(topics_sem);
      write(tcp_client_n, msg, BUF_SIZE);
    }else if (strcmp(cmd, "LEAVE") == 0){
      sprintf(msg, "OK");
      write(tcp_client_n, msg, BUF_SIZE);
      sem_wait(users_sem);
      shm->n_childs--;
      sem_post(users_sem);
      close(tcp_client_n);
      return;
    }else{
      // If the message sent by the client is equal to "SAIR" the server responds with the message "Ate logo!" 
      sprintf(msg, "UNKNOWN COMMAND");
      write(tcp_client_n, msg, BUF_SIZE);
      break;
    }
	  fflush(stdout);
  } while (1);
}

void *udp_server(){
  struct sockaddr_in si_udp;
  socklen_t slen = sizeof(si_upd_client);
  char buf[BUFLEN], cmd[BUFLEN], username[SIZE], password[SIZE], role[SIZE];
  unsigned int current_user = 0;
  int new_user = 1;
  int leave = 0;

  udp_thread = 1;

  // Creates a socket to sent UDP packets
	if((udp_socket=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		error("Erro na criação do socket");
	}

  // Filling UDP socket
	si_udp.sin_family = AF_INET;
	si_udp.sin_addr.s_addr = htonl(INADDR_ANY);
	si_udp.sin_port = htons(port_cofig);

	// Bind the socket with the ip
	if(bind(udp_socket,(struct sockaddr*)&si_udp, sizeof(si_udp)) == -1) {
		error("Erro no bind multicast");
	}
 
  while (!leave){
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
      for(int i = 0; i < shm->users_size; i++){
        strcat(buf, shm->users[i].username);
        strcat(buf, shm->users[i].role == 1 ? "\t\tadmin\n" : shm->users[i].role == 2 ? "\t\tjournalist\n" : "\t\tclient\n");
      }
      sem_post(users_sem);
    }else if(strcmp(cmd, "DEL") == 0){
      if(sscanf(buf, " %s %s", cmd, username) != 2)
        sprintf(buf, "ERROR: use format DEL {username}\n");
      else{
        sem_wait(users_sem);
        sprintf(buf, "User %s not found\n", username); 
        for(int i = 0; i < shm->users_size; i++){
          if(strcmp(shm->users[i].username, username) == 0){
            shm->users[i] = shm->users[shm->users_size-1];
            shm->users_size--;
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
          if(shm->users_size < MAX_USERS){
            for(int i = 0; i < shm->users_size; i++){
              if(strcmp(shm->users[i].username, username) == 0){
                sprintf(buf, "ERROR: username already exists\n");
                new_user = 0;
                break;
              }
            }
            if(new_user){
              strcpy(shm->users[shm->users_size].username, username);
              strcpy(shm->users[shm->users_size].password, password);
              shm->users[shm->users_size].role = strcmp(role, "admin") == 0 ? 1 : strcmp(role, "journalist") == 0 ? 2 : 3;
              shm->users_size++;
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
      leave = 1;
    }else{
      sprintf(buf, "ERROR: invalid command\n");
    }
    if(sendto(udp_socket, buf, strlen(buf)+1, 0, (struct sockaddr *)&si_upd_client, slen) == -1){
      error("Erro no sendto");
    }
  }
  udp_thread = 0;
  cleanup();
  pthread_exit(NULL);
}

void tcp_server(){
  struct sockaddr_in addr, client_addr;
  int client_addr_size;

  bzero((void *) &addr, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(tcp_port);

  if ( (tcp_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	  error("na funcao socket");
  if ( bind(tcp_socket,(struct sockaddr*)&addr,sizeof(addr)) < 0)
	  error("na funcao bind");
  if( listen(tcp_socket, 5) < 0) //! change to increase number of connections
	  error("na funcao listen");

  client_addr_size = sizeof(client_addr);

  while (1) {
    //wait for new connection
    tcp_client_n = accept(tcp_socket,(struct sockaddr *)&client_addr,(socklen_t *)&client_addr_size);
    
    if (tcp_client_n > 0) {
      if ((shm->worker_pid[shm->n_childs] = fork()) == 0) {
        sem_wait(users_sem);
        shm->n_childs++;
        sem_post(users_sem);
        close(tcp_socket);
        tcp_client();
        printf("CLOSED THE WORKER\n");
        exit(0);
      }
    close(tcp_client_n);
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

  if((topics_sem = sem_open("topics_sem", O_CREAT | O_EXCL, 0700, 1)) == SEM_FAILED){
      if((topics_sem = sem_open("topics_sem", 0)) == SEM_FAILED){
        error("Error opening semaphore");
      }
  }

  if((shmid = shmget(IPC_PRIVATE, sizeof(Shm)+sizeof(User)*MAX_USERS + sizeof(Topic)*MAX_TOPICS, IPC_CREAT | 0777)) == -1){
    error("Error creating shared memory");
  }

  if((shm = (Shm*)shmat(shmid, NULL, 0)) == (void*)-1){
    error("Error attaching shared memory");
  }
  shm->users = (User*)((char*)shm + sizeof(Shm));
  shm->topics = (Topic*)((char*)shm + sizeof(Shm) + sizeof(User)*MAX_USERS);
  shm->n_childs = 0;
  shm->users_size = 0;
  shm->topics_size = 0;
  shm->port = 1025;
}

void ctrlc_handler(){
  if(getpid() == parent_pid){
    printf("\nSIGINT recived\n");
    cleanup();
  }else{
    leave = 1;
  }
}

int main(int argc, char* argv[]) { 
  parent_pid = getpid();

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
