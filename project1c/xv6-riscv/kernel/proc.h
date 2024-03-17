// Saved registers for kernel context switches.
struct context
{
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// Per-CPU state.
struct cpu
{
  struct proc *proc;      // The process running on this cpu, or null.
  struct context context; // swtch() here to enter scheduler().
  int noff;               // Depth of push_off() nesting.
  int intena;             // Were interrupts enabled before push_off()?

  int runCount;        // number of times that the process has been scheduled to run on CPU
  int systemcallCount; // no. Of times that the process has made system calls
  int interruptCount;  // no. Of interrupts that have been made when this process runs
  int preemptCount;    // no. Of times that the process is preempted
  int trapCount;       // no. Of times that the process has trapped from the user mode to the kernel mode
  int sleepCount;      // no. Of times that the process voluntarily given up CPU
};

extern struct cpu cpus[NCPU];

// per-process data for the trap handling code in trampoline.S.
// sits in a page by itself just under the trampoline page in the
// user page table. not specially mapped in the kernel page table.
// uservec in trampoline.S saves user registers in the trapframe,
// then initializes registers from the trapframe's
// kernel_sp, kernel_hartid, kernel_satp, and jumps to kernel_trap.
// usertrapret() and userret in trampoline.S set up
// the trapframe's kernel_*, restore user registers from the
// trapframe, switch to the user page table, and enter user space.
// the trapframe includes callee-saved user registers like s0-s11 because the
// return-to-user path via usertrapret() doesn't return through
// the entire kernel call stack.
struct trapframe
{
  /*   0 */ uint64 kernel_satp;   // kernel page table
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
  /*  16 */ uint64 kernel_trap;   // usertrap()
  /*  24 */ uint64 epc;           // saved user program counter
  /*  32 */ uint64 kernel_hartid; // saved kernel tp
  /*  40 */ uint64 ra;
  /*  48 */ uint64 sp;
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

enum procstate
{
  UNUSED,
  USED,
  SLEEPING,
  RUNNABLE,
  RUNNING,
  ZOMBIE
};




// MLFQ scheduler functions
void MLFQ_scheduler(struct cpu *c);
void RR_scheduler(struct cpu *c);

// Define struct MLFQInfoReport
#define MLFQ_MAX_LEVEL 10

// Struct to hold MLFQ process information
struct MLFQInfoReport {
    int addedToMLFQ;                    // Flag indicating if process is added to MLFQ queue
    int priority;                       // Current priority of the process
    int ticks[MLFQ_MAX_LEVEL];          // Ticks the process has run at each priority level
    int ticksAtMaxPriority;             // Ticks the process has stayed at priority m-1
    int tickCounts[MLFQ_MAX_LEVEL];     // Add tickCounts member
};

// Declare system call prototypes
int startMLFQ(int m, int n);
int stopMLFQ();
int getMLFQInfo(struct MLFQInfoReport *report);



// MLFQ queue element
struct mlfqQueueElement {
    struct proc *proc; // Pointer to process
    struct mlfqQueueElement *next; // Pointer to next element
    struct mlfqQueueElement *prev; // Pointer to previous element
};

// MLFQ queue structure
struct mlfqQueue {
    struct mlfqQueueElement *head; // Head element of the queue
};

// Define maximum number of MLFQ levels
#define MLFQ_MAX_LEVEL 10

// Declare MLFQ queues globally
extern struct mlfqQueue mlfqQueues[MLFQ_MAX_LEVEL];



// Function declarations
void mlfq_enque(struct mlfqQueue *queue, struct proc *proc);
struct proc *mlfq_deque(struct mlfqQueue *queue);
void mlfq_delete(struct mlfqQueue *queue, struct proc *proc);





// Per-process state
struct proc
{
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state; // Process state
  void *chan;           // If non-zero, sleeping on chan
  int killed;           // If non-zero, have been killed
  int xstate;           // Exit status to be returned to parent's wait
  int pid;              // Process ID

  // wait_lock must be held when using this:
  struct proc *parent; // Parent process

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)



  // Added thesse fields which are used in getschedhistory
  int runCount;        // # of times that the process has been scheduled to run
  int systemcallCount; // # of times that the process has made system calls
  int interruptCount;  // # of interrupts that have been made when this process runs
  int preemptCount;    // # of times that the process is preempted
  int trapCount;       // # of times that the process has trapped from the user mode to the kernel mode
  int sleepCount;      // # of times that the process voluntarily given up CPU

  //Added the MLFQ
  struct MLFQInfoReport mlfqInfo;
};

