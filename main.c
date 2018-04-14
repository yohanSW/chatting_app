
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <regex.h>
#include <getopt.h>
#include <limits.h>
#include <semaphore.h>
#include <pthread.h>
//#include "log.h"
//#include "llist2.h"
#include "bool.h"
#include "colors.h"


/* Define some constants */
//#define SHM_ID "/mmap-test"
#define PAGE_SIZE 4096
#define BUFFER_SIZE 1024   // should be less than SEM_VALUE_MAX (32767)
#define MAX_SEQ_NO 100000
#define SLEEP_NANOS 1000   // 1 microsecond

#define APP_NAME        "CHATPROGRAM" /* Name of applicaton */
#define APP_VERSION     "1.0"     /* Version of application */

/* Structs */
typedef struct client_info {
  int clientId;
  char nickname[20];
} client_info;

typedef struct _mesg {
  char m_nickname[20];
  char m_privateTarget[20];
  char m_data[1024];
}mesg;

struct ring_buffer {
  size_t head;
  size_t tail;
  pthread_rwlock_t shMutex;
  int clientCnt;
  int newId;
  char clientList[100][20];
  size_t readLoc[100];
  mesg buffer[BUFFER_SIZE];
};

/* Function prototypes */
void cleanup(void);
void openerMem(void);
void serverFuc(void);
void sharedMem(void);
void writeLoop(void);
void readLoop(void);
void show_gnu_banner(void);
void process_msg(char *message);
void send_broadcast_msg(char* format, ...);
void send_private_msg(char* nickname, char* format, ...);

/* Global vars */
char *SHM_ID;
struct ring_buffer *prb;
size_t shmsize = ((sizeof(struct ring_buffer)+PAGE_SIZE-1)/PAGE_SIZE)*PAGE_SIZE;
struct timespec tss = {0, SLEEP_NANOS};
extern int errno;
pthread_t thread;
client_info client;

/* Main program */
int main(int argc, char* argv[])
{
  if (argc != 3) {
    printf("usage: %s {opener|client} {pathname_for_shared_memory}\n", argv[0]);
    exit(1);
  }
  
  SHM_ID = argv[2];
  show_gnu_banner();

  if (strcmp(argv[1], "opener") == 0) 
    openerMem();
  else if(strcmp(argv[1], "client") == 0) 
    sharedMem();
  else {
    printf("usage: %s {opener|client} {pathname_for_shared_memory}\n", argv[0]);
    exit(1);
  }
  pthread_create(&thread, NULL, (void *)&readLoop, NULL);
  writeLoop();

  exit(0);
}

void openerMem(void){
  int fd, seq;
  //pthread_t serverThread;
  shm_unlink(SHM_ID); // make sure that the shared memory is destroyed
  if ((fd = shm_open(SHM_ID, O_RDWR | O_CREAT, 0600)) < 0) {
    fprintf(stderr, "shm_open(): %s\n", strerror(errno));
    exit(1);
  }

  if (ftruncate(fd, shmsize) < 0) {
    fprintf(stderr, "ftruncate(): %s\n", strerror(errno));
    exit(1);
  }

  prb = (struct ring_buffer *)
    mmap(0, shmsize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (prb == MAP_FAILED) {
    fprintf(stderr, "mmap(): %s\n", strerror(errno));
    exit(1);
  }
  close(fd);

  if (atexit(cleanup) < 0) {
    fprintf(stderr, "atexit(): %s\n", strerror(errno));
    exit(1);
  }

  pthread_rwlock_init(&prb->shMutex,NULL);
  //prb->shMutex = PTHREAD_RWLOCK_INITIALIZER;
  prb->tail = prb->head = 1;
  prb->clientCnt = 1;
  prb->newId=prb->clientCnt;
  sprintf(prb->clientList[prb->newId-1], "anonymous_%d", prb->newId);
  strcpy(prb->clientList[prb->newId], "\0");
  client.clientId = prb->newId;
  strcpy(client.nickname, prb->clientList[0]);
  prb->readLoc[client.clientId-1] = prb->head-1;
  prb->readLoc[client.clientId]= (size_t)NULL;
}
 
void sharedMem(void){
  int fd;
  if ((fd = shm_open(SHM_ID, O_RDWR, 0600)) < 0) {
    fprintf(stderr, "shm_open(): %s\n", strerror(errno));
    exit(1);
  }

  if (ftruncate(fd, shmsize) < 0) {
    fprintf(stderr, "ftruncate(): %s\n", strerror(errno));
    exit(1);
  }

  prb = (struct ring_buffer *)
    mmap(0, shmsize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (prb == MAP_FAILED) {
    fprintf(stderr, "mmap(): %s\n", strerror(errno));
    exit(1);
  }
  close(fd);

  if (atexit(cleanup) < 0) {
    fprintf(stderr, "atexit(): %s\n", strerror(errno));
    exit(1);
  }
  
  pthread_rwlock_wrlock(&prb->shMutex);
  prb->clientCnt++;
  prb->newId++;
  sprintf(prb->clientList[prb->newId-1], "anonymous_%d", prb->newId);
  strcpy(prb->clientList[prb->newId], "\0");
  client.clientId = prb->newId;
  strcpy(client.nickname, prb->clientList[client.clientId-1]);
  prb->readLoc[client.clientId-1] = prb->head-1;
  prb->readLoc[client.clientId]= (size_t)NULL;
  pthread_rwlock_unlock(&prb->shMutex);
}

void serverFuc(void){
	int i, min;
		for(i=0,min=99999999;prb->readLoc[i]!=(size_t)NULL;i++)
			if(min > prb->readLoc[i])
				min = prb->readLoc[i];
		prb->head = min;
}

void cleanup(void) {
  pthread_rwlock_wrlock(&prb->shMutex); // decrement psemp
  prb->clientCnt--;
  prb->readLoc[client.clientId-1]= 99999999;
  strcpy(prb->clientList[client.clientId-1], " ");
  pthread_rwlock_unlock(&prb->shMutex); // increment psemc
}

void writeLoop(void){
  int seq;
  char buffer[1024], message[1024];
  
  while(1) {
	gets(message);
#if 1
	char *pos, *ppos = message;
	while ((pos = strstr(ppos, "\n")) != NULL) {
		*pos = 0;
		/* Process message */
		printf("ppos : %s\n",ppos);
		process_msg(ppos);
		ppos = pos + 1;
    }
	memset(buffer, 0, 1024);	
    strcpy(buffer, ppos);
    memset(message, 0, 1024);	
    strcpy(message, buffer);
	process_msg(message);
#else
	char *pos = strstr(message, "\n");
    if (pos != NULL) {		  		
		chomp(message);

	/* Process message */
		printf("message2 : %s\n",message);
		process_msg(message);
		memset(message, 0, 1024);	
    }
#endif
  }
}

void readLoop(void){
  mesg msg;
  int seq = -1;
  while(1) {
    if(seq >= (int)prb->tail){
		continue;
	}
    pthread_rwlock_rdlock(&prb->shMutex); // decrement psemc
    if (seq == -1) { // initialize seq and pid
      seq = prb->readLoc[client.clientId-1];
      msg = prb->buffer[seq%BUFFER_SIZE];
	  prb->readLoc[client.clientId-1] = ++seq;
	  pthread_rwlock_unlock(&prb->shMutex); // increment psemp
	  continue;
    }
    else
      msg = prb->buffer[seq%BUFFER_SIZE];
	prb->readLoc[client.clientId-1] = ++seq;
	serverFuc();
	pthread_rwlock_unlock(&prb->shMutex); // increment psemp
	
	if(!strcmp(msg.m_privateTarget,"\0")||!strcmp(msg.m_privateTarget,client.nickname))
		printf("%s%s : %s%s\r\n", color_green, msg.m_nickname, color_normal , msg.m_data);
    if (seq == MAX_SEQ_NO) {
      printf("consumerLoop(): %d messages received.\n", seq);
      exit(0);
    }
  }
}

void show_gnu_banner(void){
  printf("kim yo han 20132054\n\n");
  printf( "%s/---------------------------------------------\\%s\r\n", color_white, color_normal);
  printf( "%s|             W E L C O M E   T O             |%s\r\n", color_white, color_normal);
  printf( "%s|                %s %s              |%s\r\n", color_white, APP_NAME, APP_VERSION, color_normal);
  printf( "%s|          Written by kim yo han 2017 06       |%s\r\n", color_white, color_normal);
  printf( "%s\\---------------------------------------------/%s\r\n", color_white, color_normal);
}

void chomp(char *s) {
  while(*s && *s != '\n' && *s != '\r') s++;
  *s = 0;
}

void process_msg(char *message){
  char buffer[1024];
  regex_t regex_quit;
  regex_t regex_nick;
  regex_t regex_msg;
  regex_t regex_me;
  regex_t regex_who;
  int ret;
  char newnick[20];
  char oldnick[20];
  char priv_nick[20];
  int processed = FALSE;
  size_t ngroups = 0;
  size_t len = 0;
  regmatch_t groups[3];
	
  memset(buffer, 0, 1024);
  memset(newnick, 0, 20);
  memset(oldnick, 0, 20);
  memset(priv_nick, 0, 20);
  
  /* Remove \r\n from message */
  chomp(message);
  /* Compile regex patterns */
  regcomp(&regex_quit, "^/quit$", REG_EXTENDED);
  regcomp(&regex_nick, "^/nick ([a-zA-Z0-9_]{1,19})$", REG_EXTENDED);
  regcomp(&regex_msg, "^/msg ([a-zA-Z0-9_]{1,19}) (.*)$", REG_EXTENDED);
  regcomp(&regex_who, "^/who$", REG_EXTENDED);

  /* Check if user wants to quit */
  ret = regexec(&regex_quit, message, 0, NULL, 0);
  if (ret == 0)	{
    pthread_rwlock_wrlock(&prb->shMutex); // decrement psemp
	send_broadcast_msg("%sUser %s has left the chat server.%s\r\n", 
		       color_magenta, client.nickname,
		       color_normal);	
    pthread_rwlock_unlock(&prb->shMutex); // increment psemc

    /* Free memory */
    regfree(&regex_quit);
    regfree(&regex_nick);
    regfree(&regex_msg);
    regfree(&regex_me);

	exit(0);
  }

  /* Check if user wants to change nick */		
  ngroups = 2;
  ret = regexec(&regex_nick, message, ngroups, groups, 0);
  if (ret == 0)	{
    processed = TRUE;
    /* Extract nickname */
    len = groups[1].rm_eo - groups[1].rm_so;
    strncpy(newnick, message + groups[1].rm_so, len);
	strcpy(buffer, "User ");
    strcat(buffer, client.nickname);
    strcat(buffer, " is now known as ");
    strcat(buffer, newnick);
    strcpy(client.nickname, newnick);
	pthread_rwlock_wrlock(&prb->shMutex); // decrement psemp
	strcpy(prb->clientList[client.clientId-1], newnick);
	
	
	send_broadcast_msg("%s%s%s\r\n", color_yellow, buffer, color_normal);
	pthread_rwlock_unlock(&prb->shMutex); // increment psemc
  }
  
  /* Check if user wants to transmit a private message to another user */
  ngroups = 3;
  ret = regexec(&regex_msg, message, ngroups, groups, 0);
  if (ret == 0)	{
    processed = TRUE;
		
    /* Extract nickname and private message */
    len = groups[1].rm_eo - groups[1].rm_so;
    memcpy(priv_nick, message + groups[1].rm_so, len);
    len = groups[2].rm_eo - groups[2].rm_so;
    memcpy(buffer, message + groups[2].rm_so, len);
		
    /* Check if nickname exists. If yes, send private message to user. 
     * If not, ignore message.
     */	
    pthread_rwlock_wrlock(&prb->shMutex); // decrement psemp
    send_private_msg(priv_nick, "%s %s%s%s\r\n", 
		       color_normal, color_red, buffer, color_normal);
 	pthread_rwlock_unlock(&prb->shMutex); // increment psemc
 }
	
  /* Check if user wants a listing of currently connected clients */
  ret = regexec(&regex_who, message, 0, NULL, 0);
  if (ret == 0)	{
    processed = TRUE;
	int i=0;
	pthread_rwlock_wrlock(&prb->shMutex);
	while(strcmp(prb->clientList[i],"\0")){
	   printf("%s\n",prb->clientList[i]);
	   i++;
	}
	printf("\n");
	pthread_rwlock_unlock(&prb->shMutex);
  }
	
  /* Broadcast message */
  if (processed == FALSE) {
    pthread_rwlock_wrlock(&prb->shMutex);
    send_broadcast_msg("%s %s\r\n",
		       color_normal, message);
	pthread_rwlock_unlock(&prb->shMutex);
  }

  /* Free memory */
  regfree(&regex_quit);
  regfree(&regex_nick);
  regfree(&regex_msg);
  regfree(&regex_me);
  regfree(&regex_who);
}

void send_broadcast_msg(char* format, ...){
  mesg *pmsg;
  int socklen = 0;
  va_list args;
  char buffer[1024];
		
  memset(buffer, 0, 1024);

  /* Prepare message */
  va_start(args, format);
  vsprintf(buffer, format, args);
  va_end(args);
  /* Send message to client */
  pmsg = &prb->buffer[prb->tail%BUFFER_SIZE];
  strcpy(pmsg->m_nickname, client.nickname);
  strcpy(pmsg->m_privateTarget, "\0");
  strcpy(pmsg->m_data, buffer);
  prb->tail++;
}

void send_private_msg(char* nickname, char* format, ...){
  mesg *pmsg;
  int socklen = 0;
  va_list args;
  char buffer[1024];
		
  memset(buffer, 0, 1024);

  /* Prepare message */
  va_start(args, format);
  vsprintf(buffer, format, args);
  va_end(args);

  /* Send message to client */
  pmsg = &prb->buffer[prb->tail%BUFFER_SIZE];
  strcpy(pmsg->m_nickname, client.nickname);
  strcpy(pmsg->m_privateTarget, nickname);
  strcpy(pmsg->m_data, buffer); 
  
  prb->tail++;  
}