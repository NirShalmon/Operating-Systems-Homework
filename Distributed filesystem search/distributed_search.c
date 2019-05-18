/*distrubuted_search.c
written by nir shalmon
follows hw4 specefications
*/
#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include<sys/types.h>
#include<dirent.h>
#include<errno.h>
#include<string.h>
#include<unistd.h>
#include<signal.h>
#include<fcntl.h>

struct queue_node{
    char *dir; //the full path of the directory
    struct queue_node *next;
};

struct queue_node *head; /*oldest inserted node - NULL if queue is emtpy*/
struct queue_node *tail; /*newest inserted node - NULL if queue is emtpy*/

/*returns 0 iff queue isn't empty*/
char is_empty(){
    return head == NULL;
}

/*pushes a directory to the queue - returns 1 if malloc faied, otherwise 0*/
char push(char *dir){
    struct queue_node *node = (struct queue_node*)malloc(sizeof(struct queue_node));
    if(node == NULL){
        return 1;
    }
    node->dir = dir;
    node->next = (struct queue_node*)NULL;
    if(is_empty()){
        head = tail = node;
    }else{
        tail->next = node;
        tail = node;
    }
    return 0;
}

/*removes oldest pushed directory from queue and returns it
assumes queue isn't empty*/
char *pop(){
    char *head_dir = head->dir;
    struct queue_node *next_node = head->next;
    free(head);
    if(next_node == NULL){
        head = tail = NULL; //queue is now empty
    }else{
        head = next_node; //move head to next element
    }
    return head_dir;
}

pthread_t *threads;
char *search_term;
pthread_mutex_t lock;
pthread_cond_t queue_not_empty;
int threads_searching = 0; //the number of threads working on a directory they poped from queue
int thread_count;
int files_found = 0;

//unlocks the mutex. if unlock fails exits the program
void safe_unlock(pthread_mutex_t *mtx){
    if(errno = pthread_mutex_unlock(mtx)){ 
        fprintf(stderr,"ERROR in pthread_mutex_unlock :%d. EXITING\n",errno);
        printf("Stopped searching, found %d files",files_found);
        exit(1);
    }
}

//locks the mutex. if lock fails exits the program
void safe_lock(pthread_mutex_t *mtx){
    if(errno = pthread_mutex_lock(mtx)){ 
        fprintf(stderr,"ERROR in pthread_mutex_lock :%d. EXITING\n",errno);
        printf("Stopped searching, found %d files",files_found);
        exit(1);
    }
} 

//searches the given directory. adds it's directories to the queue
void search_dir(char *dirname){
    DIR *dir = opendir(dirname);
    if(dir == NULL){
        fprintf(stderr,"ERROR in opendir. Error code: %d. path: %s\n",errno,dirname);
        __sync_fetch_and_sub(&threads_searching,1);
        pthread_exit((void*)1);
    }
    errno = 0; //reset errno to cheak when readdir fails. errno is different foreach thread
    struct dirent *entry;
    while((entry = readdir(dir)) != NULL){ //read next directory enrty. thread safe in GNU c because only one thread is using this DIR
        if(entry->d_type == DT_DIR){ 
            if(strcmp(entry->d_name,".") == 0 || strcmp(entry->d_name,"..") == 0){
                continue; //this is the current/parent directory. ignore.
            }
            int child_dir_path_length = (strlen(dirname) + strlen(entry->d_name) + 2);
            char *child_dir = (char*)malloc(sizeof(char) * child_dir_path_length); //allocate memory for the full path of the child directory(remember the / and \0 chars).
            snprintf(child_dir,child_dir_path_length,"%s/%s",dirname,entry->d_name);
            safe_lock(&lock); //acquire lock before we use the queue
            push(child_dir);
            pthread_cond_signal(&queue_not_empty); //wakup a waiting thread
            safe_unlock(&lock); //we are done using the queue
        }else{
            if(strstr(entry->d_name,search_term) != NULL){ //if the file name contains the search term
                __sync_fetch_and_add(&files_found,1);
                printf("%s/%s\n",dirname,entry->d_name);
            }
        }
        pthread_testcancel(); //check if a cancel request is queued.
    }
    if(errno != 0){
        fprintf(stderr,"ERROR in readdir. Error code: %d\n",errno);
        __sync_fetch_and_sub(&threads_searching,1);
        pthread_exit((void*)1);
    }
    while(closedir(dir) != 0){
        if(errno != EINTR){ //closedir may fail due to an interrupt. in this case try again.
            fprintf(stderr,"ERROR in closedir. Error code: %d\n",errno);
            __sync_fetch_and_sub(&threads_searching,1);
            pthread_exit((void*)1);
        }
    }
    __sync_fetch_and_sub(&threads_searching,1);
}

void cleanup_handler(void *arg){
    pthread_mutex_unlock(&lock);
}

/*this function will be called on each thread with t being the thread index*/
void *do_search(void* t){
    pthread_cleanup_push(cleanup_handler,NULL);
    pthread_testcancel();
    safe_lock(&lock); //only 1 thread may use queue at the same time
    while(1){ //keep looking for new directories to search
        while(!is_empty()){ //when we have a directory waiting in queue, we use it
            char *dir = pop(); //we found a dir to search
            __sync_fetch_and_add(&threads_searching,1);
            safe_unlock(&lock); //we don't need to use the queue anymore
            search_dir(dir);
            free(dir);
            pthread_testcancel();
            safe_lock(&lock);
        }
        if(threads_searching == 0){ //we are in lock so queue is still empty.
                                    //no thread is working on a directory.
                                    //thus, the queue will always remain empty
                                    //this means we are done searching
            safe_unlock(&lock); //we wont use queue anymore.
           // pthread_testcancel(); //all new threads not yet sleeping will exit from here
            for(int i = 0; i < thread_count; ++i){
                  pthread_cancel(threads[i]); //we can ignore the only error case(no thread with this id, so nothing to cancel)
            }
            pthread_cond_broadcast(&queue_not_empty);
            pthread_exit(NULL); //exit this thread
        }
        if(errno = pthread_cond_wait(&queue_not_empty,&lock)){ //wait until queue isn't empty
            fprintf(stderr,"ERROR in pthread_cond_wait. Error code: %d\n",errno);
            printf("Stopped searching, found %d files",files_found);
            exit(1);
        }
    }
    pthread_cleanup_pop(NULL);
}

struct sigaction sigint_action;
char canceled = 0;

void sigint_handler(int signum, siginfo_t *info, void* ptr){
    canceled = 1;
	for(int i = 0; i < thread_count; ++i){
        pthread_cancel(threads[i]);
    }
    pthread_cond_broadcast(&queue_not_empty);
}


// prepare and finalize calls for initialization and destruction of anything required
int prepare_handlers(){
	//prepere sigint
	memset(&sigint_action,0,sizeof(sigaction));
	sigint_action.sa_sigaction = sigint_handler;
	sigint_action.sa_flags = SA_SIGINFO | SA_RESTART;
	if(sigaction(SIGINT,&sigint_action,NULL) != 0){
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

int main(int argc, char** argv){
    if(argc < 4){
        fprintf(stderr,"Not enough parameters\n");
        return 1;
    }
    search_term = argv[2];
    thread_count = atoi(argv[3]); //assumes a valid number greater than zero*/
    threads = (pthread_t*)malloc(thread_count * sizeof(pthread_t));
    if(threads == NULL){
        fprintf(stderr,"ERROR in malloc. Error code: %d\n",errno);
        return 1;
    }
    char* base_dir = malloc(sizeof(char) * (strlen(argv[1]) + 1)); //we need the base directory in a dinamicaly allocated string
    if(base_dir == NULL){
        fprintf(stderr,"ERROR in malloc. Error code: %d\n",errno);
        return 1;
    }
    strcpy(base_dir,argv[1]);
    push(base_dir); //push the starting directory to directory queue
    if(errno = pthread_mutex_init(&lock,NULL)){
        fprintf(stderr,"ERROR in pthread_mutex_init. Error code: %d\n",errno);
    }
    if(errno = pthread_cond_init(&queue_not_empty,NULL)){
        fprintf(stderr,"ERROR in pthread_cond_init. Error code: %d\n",errno);
    }
    //For portability, explicitly creat threads in a joinable state
    pthread_attr_t attr;
    if(pthread_attr_init(&attr)){
        fprintf(stderr,"ERROR in pthread_attr_init. Error code: %d\n",errno);
    }
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_JOINABLE);
    prepare_handlers();
    for(int i = 0; i < thread_count; ++i){
        if(errno = pthread_create(&threads[i],&attr,do_search,(void*)i)){
            fprintf(stderr,"ERROR in pthread_create for thread #%d. Error code: %d\n",i,errno);
            return 1;
        }
    }
    int failed_threads = 0;
    for(int i = 0; i < thread_count; ++i){
        void *status;
        if(errno = pthread_join(threads[i],&status)){
            fprintf(stderr,"ERROR in pthread_join. Error code: %d\n",errno);
            return 1;
        }
        if((int)status != 0 && status != PTHREAD_CANCELED) failed_threads++;
    }
    if(canceled){
         printf("Search stopped, found %d files",files_found);
    }else{
        printf("Done searching, found %d files",files_found);
    }
    pthread_attr_destroy(&attr);
    pthread_cond_destroy(&queue_not_empty);
    pthread_mutex_destroy(&lock);
    free(threads);
    return failed_threads != 0;
}