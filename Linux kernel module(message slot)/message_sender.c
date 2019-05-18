#include<stdio.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<sys/ioctl.h>
#include<fcntl.h>
#include<string.h>
#include<unistd.h>
#include<stdlib.h>
#include "message_slot.h"

int main(int argc, char const *argv[])
{
    int fd = open(argv[1],O_WRONLY);
    if(fd == -1){
        fprintf(stderr,"ERROR opening file\n");
        return 1;
    }
    int channel = atoi(argv[2]);
    if(ioctl(fd,MSG_SLOT_CHANNEL,channel) == -1){
        fprintf(stderr,"ERROR in ioctl\n");
        return 1;
    }
    if(write(fd,argv[3],strlen(argv[3])) != strlen(argv[3])){
        fprintf(stderr,"ERROR in write\n");
        return 1;
    }
    if(close(fd) == -1){
        fprintf(stderr,"ERROR in close\n");
        return 1;
    }
    printf("message written succesfully\n");
    return 0;
}
