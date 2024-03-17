struct stat;

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int *);
int pipe(int *);
int write(int, const void *, int);
int read(int, void *, int);
int close(int);
int kill(int);
int exec(const char *, char **);
int open(const char *, int);
int mknod(const char *, short, short);
int unlink(const char *);
int fstat(int fd, struct stat *);
int link(const char *, const char *);
int mkdir(const char *);
int chdir(const char *);
int dup(int);
int getpid(void);
char *sbrk(int);
int sleep(int);
int uptime(void);

// ulib.c
int stat(const char *, struct stat *);
char *strcpy(char *, const char *);
void *memmove(void *, const void *, int);
char *strchr(const char *, char c);
int strcmp(const char *, const char *);
void fprintf(int, const char *, ...);
void printf(const char *, ...);
char *gets(char *, int max);
uint strlen(const char *);
void *memset(void *, int, uint);
void *malloc(uint);
void free(void *);
int atoi(const char *);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);

// Added system call declarations to specify
//  the function interface for user programs
int getppid(void);
struct ps_struct
{
    int pid;
    int ppid;
    char state[10];
    char name[16];
};
int ps(char *psinfo);
struct sched_history
{
    int runCount;
    int systemcallCount;
    int interruptCount;
    int preemptCount;
    int trapCount;
    int sleepCount;
};
int getschedhistory(char *history);



// Define struct MLFQInfoReport
#define MLFQ_MAX_LEVEL 10

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

