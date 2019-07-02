#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);
extern char end[]; // first address after kernel loaded from ELF file
uint total_of_pages_in_system;
uint free_pages_in_system;

// extern long long total_of_pages_in_system;
// extern long long free_pages_in_system;

static void wakeup1(void *chan);

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

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;
  #ifndef NONE
    p->time_load_counter = 0;
    p->temp_page.buffer = 0;
    p->temp_page.va = 0;
    if(p->pid >2){
      if(createSwapFile(p)!=0){
        cprintf("failed to create swapfile\n");
        return 0;
      }    
    }  
    p->min_swapfile_offset = 0; 
  #endif

  p->page_faults_counter = 0;
  p->paged_out_counter = 0;
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

struct page_data*
get_page_data(struct page_data* pages_meta_data, void* va){
  struct page_data *pd;   
  for(pd = pages_meta_data ; pd < &pages_meta_data[MAX_PSYC_PAGES]; pd++){  
    if(pd->used) 
      // cprintf("pd->va: %x  va: %x\n", pd->va, va);
    if (pd->va == va){
      return pd;
    }
  }
  return 0;
}

int
update_upages(struct page_data* pages_meta_data, void* va){
  struct page_data *pd = get_page_data(pages_meta_data,va);
  if (pd!=0){
    pd->used = 0;
    pd->va = 0;
    pd->fileOffset = -1;
    return 0;
  }   
  return -1;
}

void
print_upages(struct page_data* pages_meta_data, char* identifier){

  cprintf("\n********print meta data for pid: %d of: %s  ***********\n", myproc()->pid, identifier);
  cprintf("current min_swapfile_offset: %d\n", myproc()->min_swapfile_offset);

  struct page_data *pd;
  for(pd = pages_meta_data ; pd < &pages_meta_data[MAX_PSYC_PAGES]; pd++){
    if(pd->used)
      cprintf("   va:  %x,  offset:   %d,  timer:%d\n",pd->va, pd->fileOffset, pd->load_time);
  }
  cprintf("*******************%s*******************\n\n",identifier);
}


int
add_to_upages(struct page_data* pages_meta_data, void* va, long long proc_load_timer){
  struct page_data *pd; 
  for(pd = pages_meta_data ; pd < &pages_meta_data[MAX_PSYC_PAGES]; pd++){
    if(pd->used==0){
      pd->used = 1;
      pd->va = va;     
      pd->fileOffset = -1;
      pd->load_time = proc_load_timer;
      proc_load_timer+=1;
      return proc_load_timer;
    }
  }
  // cprintf("in add_to_upages, didn't find unused place\n");
  return -1;
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
  if((p->time_load_counter = add_to_upages(p->pages_IN, 0, p->time_load_counter)) < 0)
    panic("userinit: couldn't place page in upages");
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
  total_of_pages_in_system = (PHYSTOP - V2P(end))/PGSIZE;
  free_pages_in_system = total_of_pages_in_system;

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

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

void 
copy_page_data(struct page_data* src, struct page_data* dst){
  dst->used = src->used;
  dst->va = src->va;
  dst->fileOffset = src->fileOffset; 
  dst->load_time = src->load_time;
}

void
copy_swapfile(struct proc *p_src, struct proc *p_dst){
  uint fileOffset = 0;
  char buffer[PGSIZE];
  int bytes;

  while ((bytes = readFromSwapFile(p_src, buffer, fileOffset, PGSIZE))>0){ 
    if(writeToSwapFile(p_dst,buffer, fileOffset, bytes)<0)
      panic("fork: error writeToSwapFile"); 
    fileOffset+=bytes;
  }
}

void copy_meta_data(struct page_data* src, struct page_data* dst){
  struct page_data *pd_src; 
  struct page_data *pd_dst; 

  for(pd_src=src, pd_dst=dst ; pd_src < &src[MAX_PSYC_PAGES]; pd_src++, pd_dst++){    
    copy_page_data(pd_src, pd_dst);    
  }
}
  

void handle_user_pages(struct proc *newproc, struct proc *curproc)
{    
  // deep copy of user pages meta data
  copy_meta_data(curproc->pages_IN, newproc->pages_IN);
  copy_meta_data(curproc->pages_OUT, newproc->pages_OUT);    
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
  #ifndef NONE
    // copy pages meta data and swap file
    np->min_swapfile_offset = curproc->min_swapfile_offset;
    handle_user_pages(np, curproc);    
  #endif
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
  #ifndef NONE
    if(myproc()->pid >2){
      // deep copy of swapFile
      copy_swapfile(curproc,np);
    }
  #endif
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

  release(&ptable.lock);
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
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
  #if TRUE
    print_proc_info(curproc);
    print_system_info();
  #endif

  #ifndef NONE
    if(curproc->pid >2){
      // close swap file.
      removeSwapFile(curproc);      
    }  
  #endif

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

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
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

void
clean_pages_meta_data(struct page_data* pages_meta_data){
  struct page_data *pd;
  for(pd = pages_meta_data ; pd < &pages_meta_data[MAX_PSYC_PAGES]; pd++){
    if(pd->used){
      pd->used = 0;
      pd->va = 0;     
      pd->fileOffset = -1;
    }
  }
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  #ifndef NONE
    struct page_data *pd_IN_parent, *pd_OUT_parent;
    struct page_data pages_IN_parent[MAX_PSYC_PAGES];
    struct page_data pages_OUT_parent[MAX_PSYC_PAGES];

    pd_IN_parent = pages_IN_parent;
    pd_OUT_parent = pages_OUT_parent;
    // init arrays
    for(; pd_IN_parent < &pages_IN_parent[MAX_PSYC_PAGES];){
      pd_IN_parent->used=0;
      pd_OUT_parent->used=0;

      pd_IN_parent++;
      pd_OUT_parent++;
    }
  #endif

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
        // backup to parent array
        #ifndef NONE
          copy_meta_data(curproc->pages_IN, pages_IN_parent);
          copy_meta_data(curproc->pages_OUT, pages_OUT_parent);
          // change to child data struct 
          copy_meta_data(p->pages_IN, curproc->pages_IN);
          copy_meta_data(p->pages_OUT, curproc->pages_OUT);
        #endif
        // clean child memory        
          freevm(p->pgdir);
        #ifndef NONE          
          // restore parent data struct
          copy_meta_data(pages_IN_parent, curproc->pages_IN);
          copy_meta_data(pages_OUT_parent, curproc->pages_OUT);
          p->time_load_counter = 0;          
        #endif
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;        
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

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
     
      switchuvm(p);
      
      p->state = RUNNING;
     
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.   

      c->proc = 0;
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
  myproc()->state = RUNNABLE;
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
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
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
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

void 
print_proc_info(struct proc* p)
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
  char *state;
  uint pc[10];

  int allocated_memory_pages = get_pages_count(p->pages_IN);
  allocated_memory_pages+= get_pages_count(p->pages_OUT);
  int paged_out = get_pages_count(p->pages_OUT);
  int protected_pages = get_write_protected_pages_count(p, p->pages_IN);
  protected_pages+= get_write_protected_pages_count(p, p->pages_OUT);

  if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
    state = states[p->state];
  else
    state = "???";
  // cprintf("%d %s %s\n", p->pid, state, p->name); // original
  cprintf("%d %s %d %d %d %d %d ",p->pid, state, allocated_memory_pages,paged_out,protected_pages, p->page_faults_counter, p->paged_out_counter);
  cprintf("%s", p->name);
  if(p->state == SLEEPING){
    getcallerpcs((uint*)p->context->ebp+2, pc);
    for(i=0; i<10 && pc[i] != 0; i++)
      cprintf(" %p", pc[i]);
  }    
}

void 
print_system_info(void)
{
  cprintf("\n%d / %d free pages in the system\n", free_pages_in_system, total_of_pages_in_system);  
}


//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{  
  struct proc *p;
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    print_proc_info(p);         
    cprintf("\n");
  }
  print_system_info();
}
