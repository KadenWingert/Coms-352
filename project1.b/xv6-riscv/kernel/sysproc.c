#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if (growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n)
  {
    if (killed(myproc()))
    {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// I Added these functions
// I added this to Implement the system call getppid
uint64
sys_getppid(void)
{
  struct proc *curproc = myproc();           // (1) Get the caller's struct proc
  struct proc *parentproc = curproc->parent; // (2) Find the parent's struct proc
  if (!parentproc)                           // Check if parent exists
    return -1;
  return parentproc->pid; // (3) Return the parent's pid
}

// I added this to define a global array to map enum procstate values to strings
const char *stateStr[] = {
    [UNUSED] = "UNUSED",
    [USED] = "USED",
    [SLEEPING] = "SLEEPING",
    [RUNNABLE] = "RUNNABLE",
    [RUNNING] = "RUNNING",
    [ZOMBIE] = "ZOMBIE",
};

// Implement the system call ps
extern struct proc proc[NPROC]; // declare array proc which is defined in proc.c already
uint64
sys_ps(void)
{

  // I added this to Define the ps_struct for each process
  struct ps_struct
  {
    int pid;
    int ppid;
    char state[10];
    char name[16];
  } ps[NPROC];

  int numProc = 0; // Variable keeping track of the number of processes in the system

  // Loop through the array proc to find active processes
  for (int i = 0; i < NPROC; i++)
  {
    if (proc[i].state != UNUSED)
    {
      // Retrieve information for each process and put it into ps_struct
      ps[numProc].pid = proc[i].pid;
      // ps[numProc].ppid = proc[i].parent->pid;
      // Check if parent exists before accessing its pid
      if (proc[i].parent)
      {
        ps[numProc].ppid = proc[i].parent->pid;
      }
      else
      {
        ps[numProc].ppid = 0; // Handle the case where parent is NULL
      }

      // // Copy state and name
      strncpy(ps[numProc].state, stateStr[proc[i].state], sizeof(ps[numProc].state));
      strncpy(ps[numProc].name, proc[i].name, sizeof(ps[numProc].name));

      numProc++;
    }
  }

  // Save the address of the user space argument to arg_addr
  uint64 arg_addr;
  argaddr(0, &arg_addr);
  // Copy array ps to the saved address
  if (copyout(myproc()->pagetable, arg_addr, (char *)ps, numProc * sizeof(struct ps_struct)) < 0)
    return -1;
  // Return numProc as well
  return numProc;
}

// I added this to Implement the system call getschedhistory
uint64
sys_getschedhistory(void)
{
  // Define the struct for reporting scheduling history
  struct sched_history
  {
    int runCount;
    int systemcallCount;
    int interruptCount;
    int preemptCount;
    int trapCount;
    int sleepCount;
  } my_history;

  // Retrieve the current process' information to fill my_history
  struct proc *curproc = myproc();
  if (curproc == 0)
    return -1; // Return -1 if no current process

  // Retrieve the current process'information to fill my_history
  my_history.runCount = curproc->runCount;
  my_history.systemcallCount = curproc->systemcallCount;
  my_history.interruptCount = curproc->interruptCount;
  my_history.preemptCount = curproc->preemptCount;
  my_history.trapCount = curproc->trapCount;
  my_history.sleepCount = curproc->sleepCount;

  // Save the address of the user space argument to arg_addr
  uint64 arg_addr;
  argaddr(0, &arg_addr);

  // Copy the content in my_history to the saved address
  if (copyout(curproc->pagetable, arg_addr, (char *)&my_history, sizeof(struct sched_history)) < 0)
    return -1;

  // Return the pid as well
  return curproc->pid;
}
