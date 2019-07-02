#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "kthread.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct spinlock locks[NPROC];
  struct mutex mutexes[MAX_MUTEXES];
  struct spinlock mutex_array_lock;
} ptable;

static struct thread *init_thread;

int nextpid = 1;
// int nexttid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

int
kthread_mutex_alloc(){
  struct mutex* m;
  int mutex_index = 0;
  acquire(&ptable.mutex_array_lock);

  for(m = ptable.mutexes; m < &ptable.mutexes[MAX_MUTEXES]; m++){
    if(EMPTY == m->state){
      goto found;
    }
    mutex_index++;
  }
  release(&ptable.mutex_array_lock);
  return -1;

found:
m->state = USED;
initlock(&m->lk, "");
m->pid = 0;
m->tid =0;
m->id = mutex_index;
m->locked = 0;
release(&ptable.mutex_array_lock);
return mutex_index;
}

int
kthread_mutex_dealloc(int mutex_id){
  acquire(&ptable.mutex_array_lock);
  struct mutex *m = &ptable.mutexes[mutex_id];
  if(m->state == USED){
    acquire(&m->lk);
    if (m->locked != 0){
      release(&m->lk);
      release(&ptable.mutex_array_lock);
      return -1;
    }
    // clean mutex
    m->state = EMPTY; 
    release(&m->lk);
    release(&ptable.mutex_array_lock);
    return 0;
  }
  release(&ptable.mutex_array_lock);
  return -1;
}

int
kthread_mutex_lock(int mutex_id){
  struct mutex *m = &ptable.mutexes[mutex_id];
  if(m->state != USED){
    return -1;
  }
  acquire(&m->lk);
  while (m->locked) {
    sleep(m, &m->lk);
  }
  m->locked = 1;
  m->pid = myproc()->pid;
  m->tid = mythread()->tid;
  release(&m->lk);  
  return 0;
}

int 
kthread_mutex_unlock(int mutex_id){
  struct mutex *m = &ptable.mutexes[mutex_id];
  if(m->state != USED || m->pid != myproc()->pid || m->tid !=mythread()->tid){
    return -1;
  }
  acquire(&m->lk);
  m->locked = 0;
  m->pid = 0;
  m->tid = 0;
  wakeup(m);
  release(&m->lk);
  return 0;
}

void
wait_to_thread(struct thread *thread_to_join,
 struct thread *curthread)
{
    struct proc *curproc = curthread->proc;
    // Wait for thread to exit.  (See wakeup1 call in proc_exit.)
    sleep(thread_to_join, curproc->ttable.lock);
}

void
kill_thread(struct thread* t){ 
  t->killed = 1;  
  // Wake process from sleep if necessary.
  if(t->state == SLEEPING)
    t->state = RUNNABLE;
}

static struct thread*
allocthread(struct proc *p)
{
  struct thread *t;
  char *sp;
  int thread_index = 0;

  acquire(p->ttable.lock);
  // cprintf("in allocthread aquire pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);

  for(t = p->ttable.threads; t < &p->ttable.threads[NTHREAD]; t++){
    if(t->state == UNUSED)
          goto found;
    thread_index++;
  }   
  release(p->ttable.lock);
  return 0;

  found:
  t->state = EMBRYO;  
  t->tid = thread_index;
  // nexttid++;
  t->proc = p;
  t->killed = 0;
  release(p->ttable.lock);

  // Allocate kernel stack.  
  if((t->kstack = kalloc()) == 0){
    t->state = UNUSED;
    return 0;
  }
  sp = t->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *t->tf;
  t->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *t->context;
  t->context = (struct context*)sp;
  memset(t->context, 0, sizeof *t->context);
  t->context->eip = (uint)forkret;
  return t;
}


//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct thread*
allocproc(void)
{
  struct proc *p;
  struct thread *t;
  int index = 0;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED){
      goto found;
    }
    index++;
  }  
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  // cprintf("current pid = %d, index=%d\n", nextpid,index);
  p->pid = nextpid++;
  p->ttable.lock = &ptable.locks[index];
  initlock(p->ttable.lock, "ttable");
  // cprintf("init to ttable for proc: %d\n", p->pid);
  release(&ptable.lock); 

  t = allocthread(p);
  if (0 == t){
    p->state = UNUSED;

    return 0;
  }
  return t;
}

int kthread_create(void(*start_func)(), void* stack)
{
  // cprintf("enter to kthread_create\n");

  struct thread* nt;
  struct proc* curproc = myproc();
  nt = allocthread(curproc);
  // cprintf("finished allocthread\n");

  if (0 == nt){
    return -1;
  }

  // cprintf("in kthread_create new thread tid: %d\n", nt->tid);
  *nt->tf = *mythread()->tf;
  nt->tf->eip = (uint) start_func;  // excecute func
  nt->tf->esp = (uint) stack ; //user stack
  // cprintf("in kthread_create aquire pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);

  acquire(curproc->ttable.lock);

  nt->state = RUNNABLE;

  release(curproc->ttable.lock);
  return nt->tid;
}


int kthread_id(void){
  struct thread* t = mythread();
  if (t!=0){
    return t->tid;
  }
  return -1; 
}

void
collect_thread (struct thread *thread){
  thread->state = UNUSED;
  // cprintf("collect_thread \n");
  if (thread->kstack!= 0){
    kfree(thread->kstack);
    thread->kstack = 0;
  }  
  thread->killed = 0;
  thread->tid = 0;
}


int 
kthread_join(int tid){
  struct thread *thread_to_join;
  struct thread *curthread = mythread();
  struct proc *curproc = curthread->proc;
  
  for(;;){
    acquire(curproc->ttable.lock);
    // cprintf("in kthread_join aquire pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);

    // Scan through table looking for thread with tid.
    thread_to_join = &curproc->ttable.threads[tid];
    if(thread_to_join->state == UNUSED){
      release(curproc->ttable.lock);
      return -1;
    }
    if(thread_to_join->state == ZOMBIE){
      collect_thread(thread_to_join);
      release(curproc->ttable.lock);
      return 0;
    }
    if (curthread->killed){
      release(curproc->ttable.lock);
      return -1;
    } 
    wait_to_thread(thread_to_join, curthread);
    if(thread_to_join->state == ZOMBIE){
      collect_thread(thread_to_join);
      release(curproc->ttable.lock);
      return 0;
    }
    else{
      panic("in kthread_join: thread_to_join shuld be zombie");
    }
  }
}

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

struct thread*
mythread(void) {
  struct cpu *c;
  struct thread *t;
  pushcli();
  c = mycpu();
  t = c->thread;
  popcli();
  return t;
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  struct thread *t;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  t = allocproc();
  p = t->proc;
  init_thread = t;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(t->tf, 0, sizeof(*t->tf));
  t->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  t->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  t->tf->es = t->tf->ds;
  t->tf->ss = t->tf->ds;
  t->tf->eflags = FL_IF;
  t->tf->esp = PGSIZE;
  t->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.

  // acquire(&ptable.lock);
  acquire(p->ttable.lock);  
  // cprintf("in userinit aquire pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);

  t->state = RUNNABLE;

  release(p->ttable.lock);
  // release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = mythread()->proc;
  // cprintf("in growproc aquire pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);

  acquire(curproc->ttable.lock);


  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(curproc->ttable.lock);
      return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(curproc->ttable.lock);
      return -1;
    }
  }
  curproc->sz = sz;  
  release(curproc->ttable.lock);
  struct thread *curthread = mythread();
  switchuvm(curthread);
  return 0;
}

//SHOULD ONLY DUPLICATE THE CALLING THREAD 

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct thread *nt;
  struct thread *curthread = mythread();
  struct proc *curproc = curthread->proc;

  // Allocate process.
  if((nt = allocproc()) == 0){
    return -1;
  }
  np = nt->proc;

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(nt->kstack);
    nt->kstack = 0;
    np->state = UNUSED;
    nt->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *nt->tf = *curthread->tf;

  // Clear %eax so that fork returns 0 in the child.
  nt->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;
  // cprintf("in fork aquire pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);

  acquire(np->ttable.lock);


  nt->state = RUNNABLE;

  release(np->ttable.lock);
  return pid;
}
void
kill_other_threads(struct thread* curthread, struct proc* proc_to_kill,
boolean wait_for_threads)
{
  // cprintf("enter to kill_other_threads\n");
  struct thread *t;

  acquire(proc_to_kill->ttable.lock);
  // cprintf("in kill_other_threads aquire pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);


  for(t = proc_to_kill->ttable.threads; t < &proc_to_kill->ttable.threads[NTHREAD]; t++){
    if (t->state != ZOMBIE && t->state != UNUSED && t!= curthread){
        kill_thread(t);      
    }    
  } 
  if (wait_for_threads){    
    for(t = proc_to_kill->ttable.threads; t < &proc_to_kill->ttable.threads[NTHREAD]; t++){
      if (t->state != ZOMBIE && t->state != UNUSED && t!= curthread){
        wait_to_thread(t, curthread);
        if(t->state == ZOMBIE){
          collect_thread(t);
        }    
      }    
    }
  } 
  release(proc_to_kill->ttable.lock);   
}
// if (wait_for_threads){
//         // cprintf("need to wait to thread: me:pid:%d, index:, other:pid:%d, index:%d\n", curthread->proc->pid, curthread->tid,
//         // t->proc->pid, t->tid);
//         wait_to_thread(t, curthread);
//         if(t->state == ZOMBIE){
//           collect_thread(t);
//         }
//       }



// assume that this function excecutes only by one thread
// so no need to acquire lock
void
terminate_process(struct proc *curproc)
{
  // cprintf("enter to terminate_process\n");
  struct proc *p;
  struct thread *t;
  int fd;
  if(mythread()->proc != curproc)
    panic("terminate_process : mythread()->proc != curproc");
  // cprintf("pid: %d, state: %d, killed: %d",mythread()->proc->pid, mythread()->state, mythread()->killed);
  // CLOSE ALL THREADS RESURCES WITHIN THE PROCESS
  for(t = curproc->ttable.threads; t < &curproc->ttable.threads[NTHREAD]; t++){
    if(t->state == ZOMBIE){       
      t->state = UNUSED;
    }
  }  
  // TODO: MOVE TO KTHREAD_EXIT
  release(curproc->ttable.lock);
  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);
  // mythread()->state = UNUSED;
  // cprintf("in terminate_process curthread->state = %d, pid: %d, index: %d\n" ,mythread()->state, mythread()->proc->pid, mythread()->tid);
  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);
  // cprintf("after wakeup1\n");
  // cprintf("wakeup father: pid: %d\n", curproc->pid);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = init_thread->proc;
      if(p->state == ZOMBIE)
        wakeup1(init_thread->proc);
    }
  }
  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;

  acquire(curproc->ttable.lock);
  // cprintf("in terminate_process aquire pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);

  mythread()->state = UNUSED;
  release(curproc->ttable.lock);

  sched();
  panic("zombie exit");
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct thread *curthread = mythread();
  struct proc *curproc = curthread->proc;
  // cprintf("cpu: %d: in exit: pid: %d\n",mycpu()->apicid, curproc->pid);

  if(curthread == init_thread){
    // cprintf("cpu: %d: curthread == init_thread\n",mycpu()->apicid);
    panic("init exiting");
  }
  boolean wait_to_threads = false;
  kill_other_threads(curthread, curproc, wait_to_threads);
  kthread_exit(curthread);
}


void
kthread_exit(void)
{
  // cprintf("in kthread_exit0 pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);
  struct thread *curthread = mythread();
  struct proc *p = curthread->proc;
  struct thread *t;
  // cprintf("in kthread_exit1 aquire pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);
  if(curthread == init_thread){
    panic("init exiting");
  }

  acquire(p->ttable.lock);
  curthread->state = ZOMBIE;
  release(p->ttable.lock);  
  // cprintf("in kthread_exit release1\n");
// synch problem?

  wakeup(curthread);
  // cprintf("in kthread_exit2 aquire pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);

  acquire(p->ttable.lock);

// cprintf("in kthread_exit pid: %d tid:%d\n",p->pid, curthread->tid);

// if this the last thread in process will exit process
  int found_other_thread_in_process = 0;
  for(t = p->ttable.threads ; t < &p->ttable.threads[NTHREAD] ; t++){
    if (t->state != ZOMBIE && t->state != UNUSED && t!= curthread){
      found_other_thread_in_process = 1;
      break;
    }
  }
  if (0 == found_other_thread_in_process){
    // cprintf("in kthread_exit last thread to run, pid: %d, index: %d\n", p->pid, curthread->tid);
    terminate_process(p);  
  }
  else{
    release(p->ttable.lock);  
    acquire(&ptable.lock);
    sched();
  }
}


// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct thread *t;
  struct thread *curthread = mythread();
  struct proc *curproc = curthread->proc;
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        // clean all threads
        // cprintf("in wait aquire pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);

        acquire(p->ttable.lock);

        for(t = p->ttable.threads; t < &p->ttable.threads[NTHREAD]; t++){
          if(t->state == ZOMBIE || t->state == UNUSED){
              collect_thread(t);
          }
        } 
        release(p->ttable.lock); 
        pid = p->pid;        
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        // p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curthread->killed){
      release(&ptable.lock);
      return -1;
    }
    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;  
  struct thread *t;

  struct cpu *c = mycpu();
  c->proc = 0;
  c->thread = 0;  
  // cprintf("start schedler for cpu: %d\n",mycpu()->apicid);
  for(;;){
    // Enable interrupts on this processor.
    sti();   
    acquire(&ptable.lock);    
    // Loop over process table looking for process to run.    
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){  
      if(p->state == UNUSED || p->state== ZOMBIE)
        continue;

      acquire(p->ttable.lock);
      // cprintf("in scheduler aquire pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);


      for(t = p->ttable.threads ; t < &p->ttable.threads[NTHREAD] ; t++){
        if(t->state != RUNNABLE)
          continue;
        else{
          break;
        }
      }      
      if (t < &p->ttable.threads[NTHREAD]) {
        // cprintf("cpu: %d, find thread to run! proc: %d , thread: %d,state = %d \n",mycpu()->apicid, p->pid,t->tid,  t->state);
       
        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = p;
        c->thread = t;
        switchuvm(t);
        // p->state = RUNNING;
        // if(t->proc->pid == 89 || t->proc->pid == 90 || t->proc->pid == 91 || t->proc->pid == 92){
        //   cprintf("changed thread state from real pid: %d, locked pid: %d, index: %d state: %d to runing in scheduler\n",t->proc->pid, p->pid,
        //    t->tid, t->state);
        // }

        t->state = RUNNING;                
        
        release(p->ttable.lock);
        // cprintf("cpu: %d, change to running state! proc: %d , thread: %d,state = %d \n",mycpu()->apicid, p->pid,t->tid,  t->state);

        swtch(&(c->scheduler), t->context);

        // cprintf("after swatch\n");
        // cprintf("proc: %d , thread: %d, state: %d\n", p->pid,t->tid,  t->state);
        switchkvm();
        // Process is done running for now.
        // It should have changed its p->state before coming back.
        // cprintf("cpu: %d, finish to run proc: %d , thread: %d,t->tf->eax = %d \n",mycpu()->apicid, p->pid,t->tid,  t->tf->eax);

        c->proc = 0;
        c->thread = 0;
      }
      else{
        release(p->ttable.lock);
      }      
    }
    release(&ptable.lock);  
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct thread *t = mythread();
  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(t->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&t->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *curproc = myproc();
  acquire(&ptable.lock); 
  acquire(curproc->ttable.lock);  //DOC: yieldlock
  // cprintf("in yield aquire pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);


  mythread()->state = RUNNABLE;
  release(curproc->ttable.lock);
  // cprintf("cpu: %d, in yield: before acquire(&ptable.lock); \n", mycpu()->apicid);


  // cprintf("in yield: before sched\n");
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock); 

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct thread *t = mythread();
  if(t == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ttable.lock in order to
  // change t->state and then call sched.
  // Once we hold ttable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk. 

  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  } 

  // Go to sleep.

  t->chan = chan;
  t->state = SLEEPING;

  sched();  

  // Tidy up.
  t->chan = 0;  
  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct thread *t;
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
        continue;  
    
    acquire(p->ttable.lock);   

    for(t = p->ttable.threads ; t < &p->ttable.threads[NTHREAD] ; t++){
      if(t->state == SLEEPING && t->chan == chan)
        t->state = RUNNABLE;
    }
    release(p->ttable.lock);   
  }    
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
  // here

}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  // cprintf("enter to kill\n");
  // here
  struct proc *p;
  struct thread* curthread = mythread();
  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      boolean wait_for_threads = false;
      kill_other_threads(curthread, p, wait_for_threads);
      if (curthread->proc == p){
        // cprintf("in kill aquire pid:%d, tid: %d\n", mythread()->proc->pid, mythread()->tid);

        acquire(p->ttable.lock);


        kill_thread(curthread);
        release(p->ttable.lock); 
      }    
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  struct thread *t;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    for(t = p->ttable.threads ; t < &p->ttable.threads[NTHREAD] ; t++){
      if(t->state == SLEEPING){
        getcallerpcs((uint*)t->context->ebp+2, pc);
        for(i=0; i<10 && pc[i] != 0; i++)
          cprintf(" %p", pc[i]);
      }
    }
    cprintf("\n");
  }
}
