#include<stdio.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<sys/ioctl.h>
#include<fcntl.h>
#include<string.h>
#include<unistd.h>
#include<errno.h>
#include<stdlib.h>
#include "message_slot.h"

int main(int argc, char const *argv[])
{
    char buffer[MAX_MESSAGE_LEN+1];
    int fd = open(argv[1],O_RDONLY);
    if(fd == -1){
        fprintf(stderr,"ERROR opening file\n");
        return 1;
    }
    int channel = atoi(argv[2]);
    if(ioctl(fd,MSG_SLOT_CHANNEL,channel) == -1){
        fprintf(stderr,"ERROR in ioctl\n");
        return 1;
    }
    int len;
    if((len = read(fd,buffer,MAX_MESSAGE_LEN)) == -1){
        fprintf(stderr,"ERROR in read: %s\n",strerror(errno));
        return 1;
    }
    if(close(fd) == -1){
        fprintf(stderr,"ERROR in close\n");
        return 1;
    }
    buffer[len] = 0;
    printf("message read succesfully - message is:\n");
    printf("%s",buffer);
    return 0;
}
