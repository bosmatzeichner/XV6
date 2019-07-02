//
// Created by nadav on 3/13/19.
//
#include "types.h"
#include "user.h"
#include "fcntl.h"

#define ROUND_ROBIN 1
#define PRIORITY 2
#define EXTENED_PRIORITY 3

// struct perf {
//     int ctime;                     // Creation time
//     int ttime;                     // Termination time
//     int stime;                     // The total time spent in the SLEEPING state
//     int retime;                    // The total time spent in the RUNNABLE state
//     int rutime;                    // The total time spent in the RUNNING state
// };
struct perf;

typedef boolean (test_runner)();

void run_test(test_runner *test, char *name) {
    printf(1, "========== Test - %s: Begin ==========\n", name);
    boolean result = (*test)();
    if (result) {
        printf(1, "========== Test - %s: Passed ==========\n", name);
    } else {
        printf(1, "========== Test - %s: Failed ==========\n", name);
    }
}

boolean assert_equals(int expected, int actual, char *msg) {
    if (expected != actual) {
        printf(2, "Assert %s failed: expected %d but got %d\n", msg, expected, actual);
        return false;
    } else return true;
}


void print_perf(struct perf *performance) {
    printf(1, "pref:\n");
    printf(1, "\tctime: %d\n", performance->ctime);
    printf(1, "\tttime: %d\n", performance->ttime);
    printf(1, "\tstime: %d\n", performance->stime);
    printf(1, "\tretime: %d\n", performance->retime);
    printf(1, "\trutime: %d\n", performance->rutime);
    printf(1, "\n\tTurnaround time: %d\n", (performance->ttime - performance->ctime));
}

int fact(int n) {
    if (!n)
        return 0;
    return n * fact(n - 1);
}

unsigned long long fact2(unsigned long long n, unsigned long long k) {
    start:
    if (n == 1) {
        return k;
    } else {
        --n;
        k = k * n;
        goto start;
    }


}

int fib(int n) {
    if (!n)
        return 1;
    return fib(n - 1) + fib(n - 2);
}


boolean test_exit_wait() {
    int status;
    boolean result = true;
    int pid;
    for (int i = 0; i < 20; ++i) {
        pid = fork();
        if (pid > 0) {
            wait(&status);
            result = result && assert_equals(i, status, "exit&wait");
            status = -5;
        } else {
            sleep(3);
            exit(i);
        }
    }
    return result;
}

boolean test_detach() {
    int status1;
    int status2;
    int status3;
    int pid;
    boolean result1;
    boolean result2;
    boolean result3;

    pid = fork();
    if (pid > 0) {
        status1 = detach(pid);
        result1 = assert_equals(0, status1, "detach - status1");

        status2 = detach(pid);
        result2 = assert_equals(-1, status2, "detach - status2");

        status3 = detach(-10);
        result3 = assert_equals(-1, status3, "detach - status3");

        return result1 && result2 && result3;
    } else {
        sleep(100);
        exit(0);
    }
}

boolean test_policy_helper(int *priority_mod, int policy) {
    int nProcs = 100;
    int pid, status;
    boolean result = true;
    for (int i = 0; i < nProcs; ++i) {
        pid = fork();
        if (pid < 0) {
            break;
        } else if (pid == 0) {
            if (priority_mod) {
                if ((i % *(priority_mod)) == 0 && policy == PRIORITY) {
                    priority(1);
                } else {
                    priority(i % (*priority_mod));
                }
            }
            sleep(10);
            exit(0);
        }
    }
    for (int j = 0; j < nProcs; ++j) {
        wait(&status);
        result = result && assert_equals(0, status, "round robin");
    }
    return result;

}

boolean test_round_robin_policy() {
    return test_policy_helper(null, null);

}

boolean test_priority_policy() {
    int priority_mod = 10;
    policy(PRIORITY);
    boolean result = test_policy_helper(&priority_mod, PRIORITY);
    policy(ROUND_ROBIN);
    return result;
}

boolean test_extended_priority_policy() {
    int priority_mod = 10;
    policy(EXTENED_PRIORITY);
    boolean result = test_policy_helper(&priority_mod, EXTENED_PRIORITY);
    policy(ROUND_ROBIN);
    return result;
}

boolean test_performance_helper(int *npriority) {
    int pid1;
    struct perf perf2;
    pid1 = fork();
    if (pid1 > 0) {
        int status1;
        wait_stat(&status1, &perf2);
        print_perf(&perf2);
    } else {
        for (int a = 0; a < 100; ++a) {
            int pid;
            struct perf perf1;

            pid = fork();
            // the child pid is pid
            if (pid > 0) {
                int status;
                sleep(5);
                wait_stat(&status, &perf1);
            } else {
                if (npriority)
                    priority(*npriority);
                int sum = 0;
                for (int i = 0; i < 100000000; ++i) {
                    for (int j = 0; j < 100000000; ++j) {
                        ++sum;
                    }
                }
                sleep(5);
                exit(0);
            }
        }
        exit(0);
    }
    return true;
}


boolean test_starvation_helper(int npolicy, int npriority) {
    boolean result = true;
    policy(npolicy);
    int nProcs = 10;
    int pid_arr[nProcs];
    int pid;
    memset(&pid_arr, 0, nProcs * sizeof(int));
    for (int i = 0; i < nProcs; ++i) {
        pid = fork();
        if (pid < 0) {
            break;
        } else if (pid == 0) {
            sleep(5);
            priority(npriority);
            for (;;) {};
        } else {
            pid_arr[i] = pid;
        }
    }
    sleep(100);
    for (int j = 0; j < nProcs; ++j) {
        if (pid_arr[j] != 0) {
            result = result && assert_equals(0, kill(pid_arr[j]), "failed to kill child (yes it does sound horrible)");
            wait(null);
        }
    }
    policy(ROUND_ROBIN);
    return result;
}

/**
 * test the growth of accumulator
 */
boolean test_accumulator() {
    return test_starvation_helper(PRIORITY, 2);
}

/** I hope this does test the case of
   starvation in extended priority
   (where the priority is 0)
*/
boolean test_starvation() {
    return test_starvation_helper(EXTENED_PRIORITY, 0);
}


boolean test_performance_round_robin() {
    return test_performance_helper(null);
}

boolean test_performance_priority() {
    policy(PRIORITY);
    int npriority = 2;
    boolean result = test_performance_helper(&npriority);
    policy(ROUND_ROBIN);
    return result;

}

boolean test_performance_extended_priority() {
    policy(EXTENED_PRIORITY);
    int npriority = 0;
    boolean result = test_performance_helper(&npriority);
    policy(ROUND_ROBIN);
    return result;

}


int main(void) {
    run_test(&test_exit_wait, "exit&wait");
    run_test(&test_detach, "detach");
    run_test(&test_round_robin_policy, "round robin policy");
    run_test(&test_priority_policy, "priority policy");
    run_test(&test_extended_priority_policy, "extended priority policy");
    run_test(&test_accumulator, "accumulator");
    run_test(&test_starvation, "starvation");
    run_test(&test_performance_round_robin, "performance round robin");
    run_test(&test_performance_priority, "performance priority");
    run_test(&test_performance_extended_priority, "performance extended priority");
    exit(0);
}

