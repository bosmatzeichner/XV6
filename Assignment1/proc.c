#include "schedulinginterface.h"
#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

extern PriorityQueue pq;
extern RoundRobinQueue rrq;
extern RunningProcessesHolder rpholder;
extern long long time_quantum_counter; //counts the number of time quantumsthat have expired

char* policy_names[4] = {"DEFULT","ROUND ROBIN", "PRIORITY", "EXTENDED PRIORITY"};

long long getAccumulator(struct proc *p) {
  return p->accumulator;
}
struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);
static int current_policy = ROUND_ROBIN;

static void wakeup1(void *chan);

void 
update_process_state_stats_after_clock(void){
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){    
    if(p->state == SLEEPING){
      (p->stime)++;
    }
    if(p->state == RUNNABLE){
      (p->retime)++;
    }  
  }
  release(&ptable.lock);
}

// add proc p to all schedule structs by current policy-- used when proc becomes RUNNABLE
boolean add_to_schedule_structs(struct proc *p){
  boolean success = false;
  switch(current_policy){
    case ROUND_ROBIN:
      success = rrq.enqueue(p);
      break;
    case PRIORITY:    
      if (0 == p->priority){
        p->priority = 1;        
      }  
      success = pq.put(p);   
      break;

    case EXTENDED_PRIORITY:    
      success = pq.put(p);      
      break;
  }
  return success;
}

struct proc * get_proc_with_lowest_execute_time(){
  struct proc *chosen_p = null;
  struct proc *p;
  long long lowest_excecute_time = 0;
  boolean start_iter = true;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state != RUNNABLE){
      continue; 
    }
    if (start_iter){
      lowest_excecute_time = p->excecute_time;
      chosen_p = p;
      start_iter = false;
    }
    else{
      if(lowest_excecute_time > p->excecute_time){
        lowest_excecute_time = p->excecute_time;
        chosen_p = p;
      }
    }
  }
 
  // remove chosen proc from pq
  if (chosen_p!= null && chosen_p->state == RUNNABLE){
    boolean success = pq.extractProc(chosen_p);    
    if (!success){
    panic("in get_proc_with_lowest_execute_time not succed to extract chosen proc from pq");
    }
  }  
  return chosen_p;     
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

void set_min_accumulator(struct proc *p){
  long long min_accumulator_rp;
  long long min_accumulator_pq;
  boolean success_rp = rpholder.getMinAccumulator(&min_accumulator_rp);  
  boolean success_pq = pq.getMinAccumulator(&min_accumulator_pq); 
  if (success_rp | success_pq){      
    if (success_rp && success_pq){
      if (min_accumulator_rp > min_accumulator_pq){
        p->accumulator = min_accumulator_pq;
      }
      else{
        p->accumulator = min_accumulator_rp;
      }
    }
    else if(success_rp){
      p->accumulator = min_accumulator_rp;
    }
    else if(success_pq){
      p->accumulator = min_accumulator_pq;
    }
  }
  else{
    p->accumulator = 0;
  } 
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  set_min_accumulator(p); 
  // the priority of a new processes is 5,
  p->priority = 5;
  p->ctime = time_quantum_counter;
  p->ttime = 0;
  p->stime = 0;
  p->retime = 0;
  p->rutime = 0;  
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;  
  add_to_schedule_structs(p); 

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;  
  add_to_schedule_structs(np);

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(int status)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

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
  curproc->status = status;

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.  
  curproc->ttime = time_quantum_counter;
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Detach a child process with the given pid from the parent to the init process.
// Return 0 ON SUCCESS and -1 if the parent dont have child with the given pid.
int
detach(int pid)
{
  int success = -1;
  struct proc *p;
  struct proc *curproc = myproc();
  acquire(&ptable.lock);

  //need to check that parent have child with the given pid.  
  for(p = ptable.proc; p < &ptable.proc[NPROC] && success == -1; p++){
    if(p->parent == curproc && p->pid == pid){
      success = 0;
      p->parent = initproc;     
      wakeup1(curproc);
    }
  }
  release(&ptable.lock);
  return success;
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(int* status)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
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
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        if (status!= 0){
          *status = p->status;  
        } 
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

// used by a process to change its priority.
// the priority of a new processes is 5, the lowest priority is 10 and the highestpriority is 0.
void
priority(int new_priority){
  if ((current_policy != PRIORITY) || (new_priority != 0)){  
    struct proc *curproc = myproc();    
    curproc->priority = new_priority;  
  }
  // cprintf("current policy: %d for proc pid: %d, in priority: from p %d to %d\n",current_policy,curproc->pid, old, curproc->priority);
}

// get as parameter policy identifier (1–for Round RobinScheduling, 2–for Priority Scheduling and 3–forExtended Priority Scheduling) 
// as an argument and changes the currently used policy
void 
policy(int new_policy){
  if(new_policy != current_policy){
    acquire(&ptable.lock);
    struct proc* p;  
    if (new_policy != EXTENDED_PRIORITY){
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        switch(new_policy){
          case ROUND_ROBIN:
            p->accumulator = 0;
            break; 
          case PRIORITY:
            if (0 == p->priority){
              p->priority = 1;           
            }
            break; 
        }
      } 
    }       
    switch(new_policy){
      case ROUND_ROBIN:
        pq.switchToRoundRobinPolicy();       
        break;

      case PRIORITY:
      case EXTENDED_PRIORITY:
        if (current_policy == ROUND_ROBIN){
          rrq.switchToPriorityQueuePolicy();
        }      
        break;     
    }
    release(&ptable.lock);
  }   
  // cprintf("changed policy from %s to %s\n", policy_names[current_policy],policy_names[new_policy]);
  current_policy = new_policy;      
}

// extracting this information and presenting it to the use
int 
wait_stat(int* status, struct perf * performance){
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
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
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        if (status!= 0){
          *status = p->status;  
        }
        if (performance!= 0){         
          performance->ctime = p->ctime;
          performance->ttime = p->ttime;
          performance->stime = p->stime;
          performance->retime = p->retime;
          performance->rutime = p->rutime;
        }
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

void run_selected_process(struct proc* p ,struct cpu *c){
  // Switch to chosen process.  It is the process's job
  // to release ptable.lock and then reacquire it
  // before jumping back to us.
  c->proc = p;
  switchuvm(p);
  rpholder.add(p); 
  p->state = RUNNING;
  long long start_runing_proc_time = time_quantum_counter;
  // cprintf("now running proc with pid: %d , and time_quantum_counter is: %d\n",p->pid,  start_runing_proc_time);
  swtch(&(c->scheduler), p->context);
  switchkvm();

  // Process is done running for now.
  // It should have changed its p->state before coming back.
  
  if (p->state == RUNNABLE){
    p->accumulator += p->priority;
    add_to_schedule_structs(p);   
  }
  if (p->state == SLEEPING){
    set_min_accumulator(p);
  }  
  c->proc = 0;
  p->rutime += time_quantum_counter-start_runing_proc_time;
  rpholder.remove(p);   
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
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    acquire(&ptable.lock);
    switch(current_policy){
      case ROUND_ROBIN:
        if (!rrq.isEmpty()){
          p = rrq.dequeue();
          run_selected_process(p, c);
        }
        break;

      case PRIORITY:
        p = pq.extractMin();
        if (p != null){
          run_selected_process(p, c);
        }
        break; 

      case EXTENDED_PRIORITY:        
        if(time_quantum_counter % 100 == 0){
           p = get_proc_with_lowest_execute_time(); 
          //  if(p!=null){
          //    cprintf("found proc_with_lowest_execute_time! proc pid: %d state: %d\n", p->pid, p->state);
          //  }
        }        
        else{
          p = pq.extractMin();          
        }       
        if (p != null){           
          run_selected_process(p, c);
        }               
        break;       
    }
    release(&ptable.lock);
  }
}

// remove proc p from schedule structs by current policy-- used when proc is not RUNNABLE anymore
// boolean remove_from_schedule_structs(struct proc *p){
//   boolean success = false;
//   switch(current_policy){    
//     case PRIORITY:
//     case EXTENDED_PRIORITY:
//       success = pq.put(p);
//       break;
//   }
//   return success;
// }


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
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  struct proc *p = myproc();
  p->state = RUNNABLE;  
  p->excecute_time = time_quantum_counter;
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
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;  
  sched();

  // Tidy up.
  p->chan = 0;

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
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
      add_to_schedule_structs(p);      
    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
        add_to_schedule_structs(p);
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
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
