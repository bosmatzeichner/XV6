#include "types.h"
#include "stat.h"
#include "user.h"
#include "kthread.h"

#define THREAD_MAX 3

// ############# tests for part 2 ###############

// start func for thread -- change to diffrent uses
void start_func1(void){
    printf(1,"start execute thread \n");
    int tid = kthread_id();
    printf(1,"hello from thread %d\n", tid);
    kthread_exit();
}
void start_func2(void){
    printf(1,"start execute thread \n");
    int tid = kthread_id();
    printf(1,"hello from thread %d\n", tid);
    kthread_exit();
}
void start_func3(void){
    printf(1,"start execute thread \n");
    int tid = kthread_id();
    printf(1,"hello from thread %d\n", tid);
    kthread_exit();
}

void
kthread_join(){
    int thread_tid[THREAD_MAX];
    // initial stack and start_func
    void (*thread_start_func[])(void) = {&start_func1, &start_func2, &start_func3};
    char* thread_stacks[THREAD_MAX];
    // create all threads
    printf(1,"create all threads\n");
    for(int i = 0;i < THREAD_MAX;i++){
        thread_stacks[i] = ((char *) malloc(MAX_STACK_SIZE * sizeof(char)))+ MAX_STACK_SIZE;
        thread_tid[i] = kthread_create(thread_start_func[i],  thread_stacks[i]);
    }
    // join all threads
    printf(1,"join all threads\n");
    for(int i = 0;i < THREAD_MAX;i++){
        int success = kthread_join(thread_tid[i]);
        free(thread_stacks[i]);
        if(success == 0){
            printf(1,"successful joining thread %d\n",i+1);
        }
        else if(success == -1){
            printf(1,"unsuccessful joining thread %d\n",i+1);
        }       
    }
    printf(1,"exit main thread\n");
    exit();
}
void start_func_exec(void){
    printf(1,"start execute thread \n");
    int tid = kthread_id();
    printf(1,"hello from thread %d now will do exec\n", tid);
    char * command;
    char *args[2];

    args[0] = "/ls";
    args[1] = 0;
    command = "/ls";    
    exec(command,args);
    printf(1,"after exec - failed to exec!!!\n");
    kthread_exit();
}

void
thread_exec_test(){
    char* stack = ((char *) malloc(MAX_STACK_SIZE * sizeof(char)))+ MAX_STACK_SIZE;
    int thread_exec_tid = kthread_create(&start_func_exec,  stack);   
    printf(1,"thread with tid:%d joining to thread with tid:%d\n",kthread_id(),thread_exec_tid);
    int success = kthread_join(thread_exec_tid);
    printf("error should been kill by exec success status:%d\n",success);
    exit();
}

void
thread_wait(){
    int pid = fork();
    if(pid==0){
        printf(1,"hello from child process\n");
        char* stack = ((char *) malloc(MAX_STACK_SIZE * sizeof(char)))+ MAX_STACK_SIZE;
        printf(1,"child process try to create thread\n");
        int thread_tid = kthread_create(&start_func_exec,  stack);   
        printf(1,"thread with tid:%d joining to thread with tid:%d\n",kthread_id(),thread_tid);
        int success = kthread_join(thread_exec_tid);
        printf("success status after joining:%d\n",success);
        free(stack);
        exit();
    }
    else{
        printf(1,"hello from parent process\n");
        wait();
        printf(1,"parent process finish waiting\n");
        exit();
    }
}



// ############# tests for part 3 ###############

int mutex_id;
int my_precious;

void
start_func_mutex(){
    printf(1,"second thread start making mass\n");   
    int result = kthread_mutex_lock(mutex_id); 
    printf(1,"result from second thread lock mutex: %d\n",result); 
    my_precious++;
    printf(1,"second thread updated my_precious: %d\n",my_precious); 
    result = kthread_mutex_unlock(mutex_id); 
    printf(1,"result from second thread unlock mutex: %d\n",result); 

    
    kthread_exit();
}
void
thread_mutex_use(){
    int mutex_id = kthread_mutex_alloc(); 
    if(mutex_id<0){
        printf(1,"failed to alloc mutex\n");
        exit();
    }    
    printf(1,"first thread succesfully allocated mutex with id:%d\n", mutex_id);
    int result = kthread_mutex_lock(mutex_id); 
    my_precious = 5;
    printf(1,"result from first thread lock mutex: %d\n",result); 
    char* stack = ((char *) malloc(MAX_STACK_SIZE * sizeof(char)))+ MAX_STACK_SIZE;
    printf(1,"first thread try to create second thread\n");
    int thread_tid = kthread_create(&start_func_mutex,  stack);   
    printf(1,"thread with tid:%d joining to thread with tid:%d\n",kthread_id(),thread_tid);
    sleep(5000);
    result = kthread_mutex_unlock(mutex_id); 
    printf(1,"first thread succesfully unlocked mutex with id:%d\n", mutex_id);

    int success = kthread_join(thread_tid);
    exit();
}






int
main(int argc, char *argv[])
{   
    int success;
    int mutex_id = kthread_mutex_alloc();
    success = kthread_mutex_lock(mutex_id);
    printf(1, "lock result: %d\n", success);
    success = kthread_mutex_unlock(mutex_id);
    printf(1, "unlock result: %d\n", success);
    success = kthread_mutex_dealloc(mutex_id);
    printf(1, "dealloc result: %d\n", success);
    exit();
}




