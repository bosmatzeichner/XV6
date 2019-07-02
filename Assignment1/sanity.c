#include "types.h"
#include "stat.h"
#include "user.h"

// TODO: update tests to check policy 3
// check to add volatile to counter counter
void
detach_test()
{
    int first_status;
    int second_status;
    int third_status;
    int pid= fork();
    // the child pid is 99
    // printf(2, "the child pid is %d\n", pid);
    if(pid > 0){
        first_status = detach(pid);  
        printf(2, "first_status is %d\n", first_status);
    // status = 0
        second_status = detach(pid); 
        printf(2, "second_status is %d\n", second_status);
    // status = -1, because this process has already
    // detached this child, and it doesn’t have
    // this child anymore.
        third_status = detach(77);
        printf(2, "third_status is %d\n", third_status);  
    // status = -1, because this process doesn’t
    // have a child with this pid.
    }    
}

int 
get_sum_of_counters(int threads_counters[]){    
    printf(2, "in get_sum_of_counters! \n"); 
    int sum = 0;
    int i;
    for(i=0;i<2;i++){
        int to_add = threads_counters[i]; 
        printf(2, "before in while: to_add = %d \n",to_add);
        sum = sum+to_add;
        printf(2, "after in while: sum = %d \n",sum); 
        i++;
        printf(2, "after threads_counters++ \n"); 
    }
    return sum;
}

int 
get_thread_index(int pid, int thread_pid[]){
    int thread_index = 0;
    int i;
    for(i=0;i<2;i++){
        if (thread_pid[i] == pid){
            return thread_index;
        }
        thread_index++;
        i++;
    }
    return -1;
}
void
print_threads_statics(int threads_counters[], int MAX_COUNTERS, int thread_priority[]){ 
    int i;
    for(i=0;i<2;i++){
        float current_counter = threads_counters[i];
        float current_thread_running_percent = current_counter/MAX_COUNTERS *100;
        printf(2, "thread with priority: %d got running time %d percent\n" ,thread_priority[i] ,current_thread_running_percent);
        i++;
    }     
}

void 
create_stupid_children_and_monitor(int next_policy, int next_next_policy){
    //array of pids
    int pid [2] = {0,0};
    // array of priorities
    int thread_priority [2] = {0, 10};
    // array of threads counters
    int threads_counters [2] = {0,0};
    int MAX_COUNTERS = 500;
    int i;
    int child_pid;
    priority(1);
    policy(next_policy);
    printf(2, "changed policy! \n");  
    for (i = 0; i < 2; i++){
        printf(2, "child %d was born! \n", i);  
        child_pid = fork();
        if(child_pid>0){ 
            pid[i] = child_pid;
        }
        else{
            int thread_index = get_thread_index(getpid(), pid);
            if (thread_index == -1){
                thread_index = i;
            }   
            priority(thread_priority[thread_index]);
            while(threads_counters[i]< MAX_COUNTERS){             
                threads_counters[thread_index]++;  
                printf(2, "child with pid %d  threads counter on:%d\n",getpid(), threads_counters[thread_index]);
            }
            printf(2, "child with pid %d went to sleep\n",getpid());
            sleep(5);
            printf(2, "child num %d exit\n",i);
            exit(0);
        }
    }
    if (next_next_policy > 0){
        sleep(10);
        policy(next_next_policy);
    }
    struct perf performance;
    int status;
    int proc_pid;
    printf(2, "\n\n=============================================\n\n");
    while((proc_pid = wait_stat(&status, &performance))> 0){ 
        printf(2,"for proc with pid: %d\n", proc_pid);
        printf(2,"performance : ctime: %d, ttime: %d\n", performance.ctime, performance.ttime);
        printf(2, "stime: %d, retime: %d, rutime: %d\n", performance.stime, performance.retime, performance.rutime);      
        
    }
}


void
policy_test(char *argv[])
{
    boolean is_next_next = false;
    argv++;
    while(*argv){
        int nextpolicy = atoi(*argv);        
        if (is_next_next){
            argv++;
            int next_next_policy = atoi(*argv);
            create_stupid_children_and_monitor(nextpolicy ,next_next_policy);
        }
        else{
            create_stupid_children_and_monitor(nextpolicy,0);
        }
        argv++; 
        printf(2, "terminal proc with pid: %d, went to sleep!\n", getpid())  ;    
        sleep(7800);
        printf(2, "terminal proc with pid: %d, finish to sleep!\n", getpid())  ;    

    }
}


int
main(int argc, char *argv[])
{    
    // detach_test();
    policy_test(argv);
    exit(0);
}



// TODO:
// POLICY SWITCH: 1->2->1
// 1->3->1
// 2->3->2  :is proc with priority 0?/ 
// 
