#pragma once

#include "spinlock.h"

#define MAX_STACK_SIZE 4000
#define MAX_MUTEXES 64

enum mutex_state { EMPTY, USED }; 


// Long-term locks for processes
struct mutex {
  uint locked;       // Is the lock held?
  struct spinlock lk; // spinlock protecting this sleep lock
  
  // For debugging:
  char *name;        // Name of lock.
  int pid;           // Process holding lock
  int tid;           // Thread holding lock
  volatile enum mutex_state state;      // mutex state
  int id;
};



/********************************
        The API of the KLT package
 ********************************/

int kthread_create(void (*start_func)(), void* stack);
int kthread_id();
void kthread_exit();
int kthread_join(int thread_id);

int kthread_mutex_alloc();
int kthread_mutex_dealloc(int mutex_id);
int kthread_mutex_lock(int mutex_id);
int kthread_mutex_unlock(int mutex_id);