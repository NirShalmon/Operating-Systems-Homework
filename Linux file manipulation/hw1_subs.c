#include<stdlib.h>
#include<string.h>
#include<fcntl.h>
#include<limits.h>
#include<unistd.h>
#include<stdio.h>

#define READ_COUNT 256
#define HW1DIR_VARNAME "HW1DIR"
#define HW1TF_VARNAME "HW1TF"

//return 1 iff a[i] == b[i] for 0<=i<len
char eq_strings(char *a, char *b,int len){
    for(int i = 0; i < len; ++i){
        if(a[i] != b[i]) return 0;
    }
    return 1;
}

int main(int argc, char** argv){
    char *hw1dir,*hw1tf;
    hw1dir = getenv(HW1DIR_VARNAME);
    if(!hw1dir) return 1;
    hw1tf = getenv(HW1TF_VARNAME);
    if(!hw1tf) return 1;
    char *combined_path = (char*)malloc(sizeof(char) * (strlen(hw1dir) + 2 + strlen(hw1tf))); //make room for /0
    if(!combined_path) return 1;
    strcpy(combined_path,hw1dir);
    strcat(combined_path,"/");
    strcat(combined_path,hw1tf);

    int fd = open(combined_path,O_RDWR);
    if(fd == -1) {
        free(combined_path);
        return 1;
    }

    //read file:
    char *file_contents = (char*)malloc(sizeof(char) * (READ_COUNT+1));
    if(!file_contents) {
        free(combined_path);
        close(fd);
        return 1;
    }
    int read_ret;
    int bytes_read = 0;
    while((read_ret = read(fd,file_contents+bytes_read,READ_COUNT)) != 0){
        if(read_ret == -1) {
            free(file_contents);
            free(combined_path);
            close(fd);
            return 1;
        }
        bytes_read += read_ret;

        char* new_mem = (char*)realloc(file_contents,bytes_read+READ_COUNT+1);
        if(!new_mem) {
            free(file_contents);
            free(combined_path);
            close(fd);
            return 1;
        }else{
            file_contents = new_mem;
        }
    }
    free(combined_path);
    if(close(fd) == -1) {
        free(file_contents);
        return 1;
    }
    file_contents[bytes_read] = '\0';
    
    int param1_len = strlen(argv[1]);
    int param2_len = strlen(argv[2]);
    for(int i = 0; i < bytes_read; ++i){
        if(i + param1_len <= bytes_read && eq_strings(file_contents+i,argv[1],param1_len)){ //there is a match
            if(fwrite(argv[2],sizeof(char),param2_len,stdout) != param2_len) {
                free(file_contents);
                return 1;
            }
            i += param1_len - 1; //advance the position in the file(skip argv[1]);
        }else{ //no match
            if(fwrite(file_contents+i,sizeof(char),1,stdout) != 1){ //write a single char
                free(file_contents);
                return 1;
            }
        }
    }

    free(file_contents);
    return 0;
}