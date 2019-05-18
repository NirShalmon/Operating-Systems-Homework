/* pcc_server.c
Written by Nir Shalmon
Implements a printable character counting server as described in hw5
Communication protocol: 4 bytes of message length(unsigned int), followed by the message
*/
#include<stdio.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<errno.h>
#include<string.h>
#include<unistd.h>
#include<netinet/in.h>
#include<signal.h>

#define MAX_PC (char)126
#define MIN_PC (char)32
#define LISTEN_QUEUE_LEN 10
#define BUFF_LEN 1024

unsigned int pcc_total[MAX_PC-MIN_PC+1]; //pcc_total[c-MIN_PC] is the count for pcc c
char data_buff[BUFF_LEN];
char sigint_happened = 0; //will mark that a sigint is waiting handeling

struct sigaction sigint_action;

void print_scc_total_and_exit(){
    for(int i = 0; i < MAX_PC-MIN_PC+1; ++i){ //print pcc_total
        printf("char '%c' : %u times\n",(char)(i+MIN_PC),pcc_total[i]);
    }
    exit(0);   
}

void sigint_handler(int signum, siginfo_t *info, void* ptr){
    sigint_happened = 1;
}


// prepare and finalize calls for initialization and destruction of anything required
void prepare_handlers(char use_sa_restart){
	//prepere sigint
	memset(&sigint_action,0,sizeof(sigaction));
	sigint_action.sa_sigaction = sigint_handler;
	sigint_action.sa_flags = SA_SIGINFO;
    if(use_sa_restart){
        sigint_action.sa_flags |= SA_RESTART;
    }
	if(sigaction(SIGINT,&sigint_action,NULL) != 0){
		fprintf(stderr,"Signal handle registration failed. %s\n",strerror(errno));
		exit(1);
	}
}

//recives a fd of a connection and handles the client to completion
void connection_handler(int connfd){
    unsigned int message_len;
    unsigned int bytes_read = 0;
    //start off by reading message length(first 4 bytes)
    while(bytes_read < sizeof(unsigned int)){
        int read_ret = read(connfd,data_buff,sizeof(unsigned int) - bytes_read);
        if(read_ret == -1){
            fprintf(stderr,"Error in read: %s\n",strerror(errno));
            exit(1);
        }
        if(read_ret == 0){
            fprintf(stderr,"Connection terminated unexpectedly\n");
            close(connfd);
            return;
        }
        bytes_read += read_ret;
    }
    message_len = ntohl(*((int*)data_buff));
    bytes_read = 0; //reset bytes read for actual message
    unsigned int pcc = 0;
    //read and process message
    while(bytes_read < message_len){
        int read_ret = read(connfd,data_buff,BUFF_LEN);
        if(read_ret == -1){
            fprintf(stderr,"Error in read: %s\n",strerror(errno));
            exit(1);
        }
        if(read_ret == 0){
            fprintf(stderr,"Connection terminated unexpectedly\n");
            close(connfd);
            return;
        }
        bytes_read += read_ret;
        for(int i = 0; i < read_ret; ++i){
            if(data_buff[i] >= MIN_PC && data_buff[i] <= MAX_PC){
                ++pcc_total[data_buff[i]-MIN_PC];
                ++pcc;
            }
        }
    }
    pcc = htonl(pcc); //convert to network long
    if(write(connfd,(char*)&pcc,sizeof(unsigned int)) == -1){ //send pcc
        fprintf(stderr,"Error in write: %s\n",strerror(errno));
        exit(1);
    }  
    if(close(connfd) == -1){
        fprintf(stderr,"Connection terminated unexpectedly\n");
    }
}

int main(int argc, char **argv){
    if(argc < 2){
        fprintf(stderr,"Not enough console parameters!\n");
        return 1;
    }
    prepare_handlers(0);
    unsigned short port = (unsigned short)atoi(argv[1]);
    int listen_fd = socket(AF_INET,SOCK_STREAM,0);
    if(listen_fd == -1){
        fprintf(stderr,"Error in socket: %s\n",strerror(errno));
        return 1;
    }
    struct sockaddr_in serv_addr,peer_addr;
    socklen_t addrsize = sizeof(struct sockaddr_in);
    memset(&serv_addr,0,addrsize);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);
    if(bind(listen_fd,(struct sockaddr*)&serv_addr,addrsize) == -1){
        fprintf(stderr,"Error in bind: %s\n",strerror(errno));
        return 1;
    }
    if(listen(listen_fd,LISTEN_QUEUE_LEN) == -1){
        fprintf(stderr,"Error in listen: %s\n",strerror(errno));
        return 1;
    }
    while(1){
        if(sigint_happened){
            print_scc_total_and_exit();
        }
        int connfd = accept(listen_fd,(struct sockaddr*)&peer_addr,&addrsize);
        if(connfd == -1){
            if(sigint_happened){ //sigint recived during accept will exit accept wil EINTR
                print_scc_total_and_exit();
            }
            fprintf(stderr,"Error in accept: %s\n",strerror(errno));
            return 1;
        }
        prepare_handlers(1); //set SA_RESTART for SIGINT so that read wont fail on SIGINT
        connection_handler(connfd);
        prepare_handlers(0); //unset SA_RESTART for SIGINT so that accept would exit on SIGINT
    }
}