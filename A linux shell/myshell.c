/*
An implementation of a shell as specified by hw2.
Written by Nir Shalmon
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

void sigint_handler(int signum, siginfo_t *info, void* ptr){
	//do nothing
}

void sigchld_handler(int signum, siginfo_t *info, void* ptr){
	int ret_val;
	while((ret_val = waitpid(-1,NULL,WNOHANG)) > 0){ //wait for all zombies
	}
	if(ret_val == -1 && errno != ECHILD){ //last wait might be waiting for a child that doesnt exist
		fprintf(stderr, "Waitpid failed. %s\n",strerror(errno));
		fflush(stderr);
		exit(1);
	}
}

// arglist - a list of char* arguments (words) provided by the user
// it contains count+1 items, where the last item (arglist[count]) and *only* the last is NULL
// RETURNS - 1 if should cotinue, 0 otherwise
int process_arglist(int count, char** arglist){
	char is_background = strcmp(arglist[count-1],"&") == 0; //check is the last arg is "&"
	int pipe_location = -1;
	if(is_background){
		arglist[count-1] = (char*)NULL; //null out the "&"(so it will not be passed as an argument in execvp)
	}
	for(int i = 0; i < count - is_background; ++i){
		if(strcmp(arglist[i],"|") == 0){ //check for a "|"
			pipe_location = i;
			arglist[pipe_location] = (char*)NULL; //null out the "|" to seperate the arglist
			break;
		}
	}
	int pipe_fd[2];
	if(pipe_location != -1){
		if(pipe(pipe_fd) == -1){ //create a pipe for the child processes
			fprintf(stderr,"pipe failed: %s\n",strerror(errno));
			return 0;
		}
	}
	sigset_t critical_signals; //mask used to block SIGINT to change signal behavior for child procceses
	if(!is_background){
		sigemptyset(&critical_signals);
		sigaddset(&critical_signals,SIGINT);
		if(sigprocmask(SIG_BLOCK,&critical_signals,NULL) == -1){
			fprintf(stderr,"sigprocmask failed: %s\n",strerror(errno));
			return 0;
		}
	}
	pid_t chld1_id = fork();
	pid_t chld2_id;
	if(chld1_id == -1){
		fprintf(stderr,"fork failed: %s\n",strerror(errno));
		return 0;
	}
	if(chld1_id == 0){//this is child process
		if(pipe_location != -1){
			if(dup2(pipe_fd[1],1) == -1){
				fprintf(stderr,"dup2 failed: %s\n",strerror(errno));
				exit(1);
			}
			if(close(pipe_fd[1]) == -1 || close(pipe_fd[0]) == -1 ){ //close original pipe file descriptors
				fprintf(stderr,"close failed: %s\n",strerror(errno));
				exit(1);
			}
		}
		if(is_background){
			if(setpgid(0,0) == -1){ //move out of forground process group
				fprintf(stderr,"setpgid failed: %s\n",strerror(errno));
				exit(1);
			}
		}else{
			if(signal(SIGINT,SIG_DFL) == SIG_ERR){
				fprintf(stderr,"sigprocmask failed: %s\n",strerror(errno));
				exit(1);
			}
			if(sigprocmask(SIG_UNBLOCK,&critical_signals,NULL) == -1){
				fprintf(stderr,"sigprocmask failed: %s\n",strerror(errno));
				exit(1);
			}
		}
		if(execvp(arglist[0],arglist) == -1){
			fprintf(stderr,"execvp failed: %s\n",strerror(errno));
			exit(1);
		}
	}else{//this is parent process
		if(pipe_location != -1){ //there is a pipe. exec process 2
			chld2_id = fork();
			if(chld2_id == -1){
				fprintf(stderr,"fork failed: %s\n",strerror(errno));
				return 0;
			}
			if(chld2_id == 0){//this is child 2 process
				if(dup2(pipe_fd[0],0) == -1){
					fprintf(stderr,"dup2 failed: %s\n",strerror(errno));
					exit(1);
				}
				if(close(pipe_fd[1]) == -1 || close(pipe_fd[0]) == -1 ){ //close original pipe file descriptors
					fprintf(stderr,"close failed: %s\n",strerror(errno));
					exit(1);
				}
				if(signal(SIGINT,SIG_DFL) == SIG_ERR){
					fprintf(stderr,"sigprocmask failed: %s\n",strerror(errno));
					exit(1);
				}
				if(sigprocmask(SIG_UNBLOCK,&critical_signals,NULL) == -1){
					fprintf(stderr,"sigprocmask failed: %s\n",strerror(errno));
					exit(1);
				}
				if(execvp(arglist[pipe_location+1],arglist+pipe_location+1) == -1){
					fprintf(stderr,"execvp failed: %s\n",strerror(errno));
					exit(1);
				}
			}else{ //this is parent process
				if(close(pipe_fd[1]) == -1 || close(pipe_fd[0]) == -1 ){ //close pipe in parent
					fprintf(stderr,"close failed: %s\n",strerror(errno));
					return 0;
				}
				if(waitpid(chld2_id,NULL,0) == -1 && errno != ECHILD){//!is_background || pipe_loaction == -1. check if signal handler already waited
					fprintf(stderr, "Waitpid failed. %s\n",strerror(errno));
					return 0;
				}
			}
		}
		if(!is_background){ 
			if(sigprocmask(SIG_UNBLOCK,&critical_signals,NULL) == -1){
				fprintf(stderr,"sigprocmask failed: %s\n",strerror(errno));
				return 0;
			}
			if(waitpid(chld1_id,NULL,0) == -1 && errno != ECHILD){ //check if signal handler already waited
				fprintf(stderr, "Waitpid failed. %s\n",strerror(errno));
				return 0;
			}	
		}
	}
	return 1;
}

struct sigaction sigchld_action,sigint_action;

// prepare and finalize calls for initialization and destruction of anything required
int prepare(void){
	//prepare sigchld
	memset(&sigchld_action,0,sizeof(sigchld_action));
	sigchld_action.sa_sigaction = sigchld_handler;
	sigchld_action.sa_flags = SA_SIGINFO | SA_RESTART;
	if(sigaction(SIGCHLD,&sigchld_action,NULL) != 0){
		fprintf(stderr,"Signal handle registration failed. %s\n",strerror(errno));
		return 1;
	}
	//prepere sigint
	memset(&sigint_action,0,sizeof(sigint_action));
	sigint_action.sa_sigaction = sigint_handler;
	sigint_action.sa_flags = SA_SIGINFO | SA_RESTART;
	if(sigaction(SIGINT,&sigchld_action,NULL) != 0){
		fprintf(stderr,"Signal handle registration failed. %s\n",strerror(errno));
		return 1;
	}
	sigset_t critical_signals; //accept all signals
	sigfillset(&critical_signals);
	if(sigprocmask(SIG_UNBLOCK,&critical_signals,NULL) == -1){
		fprintf(stderr,"sigprocmask failed: %s\n",strerror(errno));
		return 0;
	}
	return 0;
}

int finalize(void){
	return 0;
}