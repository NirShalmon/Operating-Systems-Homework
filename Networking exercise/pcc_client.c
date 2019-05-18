/* pcc_client.c
Written by Nir Shalmon
Implements a printable character counting client as described in hw5
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
#include<arpa/inet.h>
#include<sys/stat.h>
#include<fcntl.h>

#define RAND_FILE "/dev/urandom"
#define MAX_WRITE 10000 //there are isues with write len too big

char buff[MAX_WRITE];

//writes len bytes from buff to fd
void write_full(int fd,unsigned int len,char* buff){
    int bytes_writen = 0;
    while(bytes_writen < len){
        int ret = write(fd,buff,(len < MAX_WRITE ? len : MAX_WRITE));
        if(ret == -1){
            fprintf(stderr,"Error in write: %s\n",strerror(errno));
            exit(1);
        }
        bytes_writen += ret;
    }
}

//reads len bytes from buff to fd
void read_full(int fd,unsigned int len,char* buff){
    int bytes_read = 0;
    while(bytes_read < len){
        int ret = read(fd,buff,len);
        if(ret == -1){
            fprintf(stderr,"Error in read: %s\n",strerror(errno));
            exit(1);
        }
        bytes_read += ret;
    }
}

int main(int argc, char **argv){
    if(argc < 4){
        fprintf(stderr,"Not enough parameters\n");
        exit(1);
    }
    unsigned short port = atoi(argv[2]);
    unsigned int len = (unsigned int)strtoul(argv[3],NULL,10);
    struct sockaddr_in serv_addr;
    int sockfd = socket(AF_INET,SOCK_STREAM,0);
    if(sockfd == -1){
        fprintf(stderr,"Error in socket: %s\n",strerror(errno));
        exit(1);
    }
    memset(&serv_addr,0,sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if(inet_aton(argv[1],&serv_addr.sin_addr) == 0){ //convert ip address to addr_in_t
        fprintf(stderr,"Invalid address\n");
        exit(1);
    }
    if(connect(sockfd,(struct sockaddr*)&serv_addr,sizeof(serv_addr)) == -1){
         fprintf(stderr,"Error in connect: %s\n",strerror(errno));
        exit(1);
    }
    int randfd = open(RAND_FILE,O_RDONLY);
    if(randfd == -1){
        fprintf(stderr,"Error in open: %s\n",strerror(errno));
        exit(1);
    }
    unsigned int len_nf = htonl(len);
    char *len_send_format = (char*)&len_nf;
  // printf("%d %d %d %d\n",len_send_format[0],len_send_format[1],len_send_format[2],len_send_format[3]);
    write_full(sockfd,sizeof(unsigned int),len_send_format);
    unsigned int bytes_left = len;
    while(bytes_left > 0){ //semd all message
        read_full(randfd,MAX_WRITE,buff);
        unsigned int to_write = (bytes_left < MAX_WRITE ? bytes_left : MAX_WRITE );
        write_full(sockfd,to_write,buff);
        bytes_left -= to_write;
    }
    read_full(sockfd,sizeof(unsigned int),buff);
    unsigned int pcc = ntohl(*(int*)buff);
    printf("$ of printable characters: %u\n",pcc);
    if(close(sockfd) == -1){
        fprintf(stderr,"Error in close: %s\n",strerror(errno));
        exit(1);
    }
    return 0;
}