#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == UNUSED)
    {
      goto found;
    }
    else
    {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;

  // Set the fields I added to 0
  p->runCount = 0;
  p->systemcallCount = 0;
  p->interruptCount = 0;
  p->preemptCount = 0;
  p->trapCount = 0;
  p->sleepCount = 0;


    // Initialize additional fields for MLFQ process information
    p->mlfqInfo.addedToMLFQ = 0;                   // Not added to MLFQ queue initially
    p->mlfqInfo.priority = 0;                      // Start with priority 0
    p->mlfqInfo.ticksAtMaxPriority = 0;            // No ticks at max priority initially
    for (int i = 0; i < MLFQ_MAX_LEVEL; i++) {
        p->mlfqInfo.ticks[i] = 0;                  // Initialize ticks at each priority level to 0
    }
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) < 0)
  {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE,
               (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
  {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;     // user program counter
  p->trapframe->sp = PGSIZE; // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
    {
      return -1;
    }
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++)
  {
    if (pp->parent == p)
    {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd])
    {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++)
    {
      if (pp->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE)
        {
          // Found one.
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0)
          {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p))
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); // DOC: wait-sleep
  }
}

int mlfqFlag;  // Define mlfqFlag
struct { 
    int m; // Number of priority levels 
    int n; // Max number of ticks a process can stay at priority level m-1 before being boosted to 0 
} mlfqParams;  // Define mlfqParams




// Function to insert a process into the MLFQ queue
void mlfq_enque(struct mlfqQueue *queue, struct proc *proc) {
    struct mlfqQueueElement *newElement = (struct mlfqQueueElement *) kalloc();
    if (newElement == 0) {
        return; // Allocation failed
    }
    newElement->proc = proc;
    newElement->next = 0;
    newElement->prev = 0;

    // Check if the queue is empty
    if (queue->head == 0) {
        queue->head = newElement;
        return;
    }

    // Find the tail of the queue
    struct mlfqQueueElement *tail = queue->head;
    while (tail->next != 0) {
        tail = tail->next;
    }

    // Insert the new element at the end
    tail->next = newElement;
    newElement->prev = tail;
}

// Function to remove and return the first process from the MLFQ queue
struct proc *mlfq_deque(struct mlfqQueue *queue) {
    if (queue->head == 0) {
        return 0; // Queue is empty
    }

    // Get the first element
    struct mlfqQueueElement *firstElement = queue->head;
    struct proc *firstProc = firstElement->proc;

    // Update the head of the queue
    queue->head = firstElement->next;
    if (queue->head != 0) {
        queue->head->prev = 0;
    }

    // Free the dequeued element
    kfree((char *) firstElement);

    return firstProc;
}

// Function to delete a specific process from the MLFQ queue
void mlfq_delete(struct mlfqQueue *queue, struct proc *proc) {
    if (queue->head == 0) {
        return; // Queue is empty
    }

    struct mlfqQueueElement *current = queue->head;
    while (current != 0) {
        if (current->proc == proc) {
            // Found the process, delete it from the queue
            if (current->prev == 0) {
                // Process is at the head of the queue
                queue->head = current->next;
            } else {
                current->prev->next = current->next;
            }
            if (current->next != 0) {
                current->next->prev = current->prev;
            }
            kfree((char *) current);
            return;
        }
        current = current->next;
    }
}


// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.

//Altered scheduler to include MLFQ_scheduler
void
scheduler(void)
{
struct cpu *c = mycpu();
c->proc=0;
for(;;){
// Avoid deadlock by ensuring that devices can interrupt.
intr_on();
if(mlfqFlag){//the flag indicates if MLFQ scheduler is running
MLFQ_scheduler(c);
}else{
RR_scheduler(c);
}
}
}

//Constructed from the existing scheduler 
void
RR_scheduler(struct cpu *c)
{
struct proc *p;
for(p = proc; p < &proc[NPROC]; p++) {
acquire(&p->lock);
if(p->state == RUNNABLE) {
// Switch to chosen process. It is the process's job
// to release its lock and then reacquire it
// before jumping back to us.
p->state = RUNNING;
c->proc = p;
swtch(&c->context, &p->context);
// Process is done running for now.
// It should have changed its p->state before coming back.
c->proc = 0;
}
release(&p->lock);
}
}


// MLFQ scheduler function
void MLFQ_scheduler(struct cpu *c) {
    struct proc *p = 0; // p is the process scheduled to run; initially it is none

    while (mlfqFlag) { // each iteration is run every time when the scheduler gains control
        // Rule 1: Find the process with the highest priority
        int highest_priority = -1;
        for (int i = 0; i < NPROC; i++) {
            if (proc[i].state == RUNNABLE && proc[i].mlfqInfo.priority > highest_priority) {
                highest_priority = proc[i].mlfqInfo.priority;
                p = &proc[i];
            }
        }

        // Rule 2: If no process is found, continue searching
        if (p == 0) continue;

        // Rule 3: Run the selected process with a time quantum of 2*(priority + 1) ticks
        if (p->state == RUNNABLE || p->state == RUNNING) {
            // Check if p's time quantum for the current queue expires or not
            if (p->mlfqInfo.ticks[p->mlfqInfo.priority] >= 2 * (p->mlfqInfo.priority + 1)) {
                // Move p to the queue below it (if p is not at the bottom queue yet)
                if (p->mlfqInfo.priority < MLFQ_MAX_LEVEL - 1) {
                    // Move the process to the next lower priority queue
                    struct mlfqQueue *lowerQueue = &mlfqQueues[p->mlfqInfo.priority + 1];
                    mlfq_enque(lowerQueue, p);
                }

                // Remove p from its current queue
                struct mlfqQueue *currentQueue = &mlfqQueues[p->mlfqInfo.priority];
                mlfq_delete(currentQueue, p);

                // Reset ticks for the current priority level
                p->mlfqInfo.ticks[p->mlfqInfo.priority] = 0;

                // Set p to 0 (to indicate another process should be found to run next)
                p = 0;
                continue;
            }

            // Run the selected process
            acquire(&p->lock);
            p->state = RUNNING;
            c->proc = p;
            swtch(&c->context, &p->context);
            c->proc = 0;
            p->mlfqInfo.ticks[p->mlfqInfo.priority]++; // Increment tick count
            release(&p->lock);
        }

        // Rule 5: After a process has been of priority number MLFQ_MAX_LEVEL - 1 for n ticks, its priority number is boosted to 0
        if (p != 0 && p->mlfqInfo.priority == MLFQ_MAX_LEVEL - 1 && p->mlfqInfo.ticksAtMaxPriority >= mlfqParams.n) {
            p->mlfqInfo.priority = 0;
        }

        // Rule 4: Once a process uses up its time quantum at its current priority level, its priority is degraded
        // (i.e. its priority number is incremented by one and it moves down one queue...if its priority number is not MLFQ_MAX_LEVEL - 1 yet.)
        if (p != 0 && p->mlfqInfo.ticks[p->mlfqInfo.priority] >= 2 * (p->mlfqInfo.priority + 1)) {
            if (p->mlfqInfo.priority < MLFQ_MAX_LEVEL - 1) {
                p->mlfqInfo.priority++;
            }
        }
    }
}

// Define MLFQ queues globally
struct mlfqQueue mlfqQueues[MLFQ_MAX_LEVEL];

// System call to start MLFQ scheduler
int startMLFQ(int m, int n) {
    // Check if MLFQ scheduler is already running
    if (mlfqFlag) {
        return -1; // MLFQ scheduler is already running
    }

    // Initialize MLFQ scheduler parameters
    mlfqFlag = 1;       // Set MLFQ scheduler flag to indicate it is running
    mlfqParams.m = m; // Set number of priority levels
    mlfqParams.n = n; // Set maximum ticks at priority m-1 before boosting to 0
    
        for (int i = 0; i < MLFQ_MAX_LEVEL; i++) {
        mlfqQueues[i].head = 0; // Initialize head pointer to NULL for each queue
    }

    return 0; // Success
}

// System call to stop MLFQ scheduler
int stopMLFQ() {
    // Check if MLFQ scheduler is not running
    if (!mlfqFlag) {
        return -1; // MLFQ scheduler is not running
    }

    // Stop MLFQ scheduler
    mlfqFlag = 0; // Clear MLFQ scheduler flag to indicate it is stopped
    
    return 0; // Success
}

// System call to get MLFQ information
int getMLFQInfo(struct MLFQInfoReport *report) {
    // Check if report pointer is valid
    if (report == 0 || !mlfqFlag) {
        return -1; // Invalid pointer or MLFQ scheduler is not running
    }

    // Initialize report structure
    for (int i = 0; i < MLFQ_MAX_LEVEL; i++) {
        report->tickCounts[i] = 0;
    }

    // Fill the report with MLFQ information
    // Traverse all processes and update tick counts for each priority level
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc[i];
        if (p->state == RUNNABLE || p->state == RUNNING) {
            report->tickCounts[p->mlfqInfo.priority] += p->mlfqInfo.ticks[p->mlfqInfo.priority];
        }
    }

    return 0; // Success
}


// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first)
  {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock); // DOC: sleeplock1
  release(lk);

  // Added this line to increment the current process' sleepCount before sched() is invoked
  p->sleepCount++;

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p != myproc())
    {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan)
      {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      p->killed = 1;
      if (p->state == SLEEPING)
      {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst)
  {
    return copyout(p->pagetable, dst, src, len);
  }
  else
  {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src)
  {
    return copyin(p->pagetable, dst, src, len);
  }
  else
  {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [USED] "used",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  struct proc *p;
  char *state;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
