// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

/*
 * Operating system 2019
 * Professor: Hyungsoo Jung
 * Student NO: 2014018940
 * Student Name: Hyung-Kwon Ko
 * Since: 2019-04-28
 * version: 0.0.0
 */

/* define required constants */
#define     TOP_QUANT    1
#define     MID_QUANT    2
#define     LOW_QUANT    4
#define     TOP_ALLOT    5
#define     MID_ALLOT    10
#define     BOOSTICK     100
#define     SUMCPU       10000

/* scheduling mode (default = stride) */
enum sched_mode { STRD_SCHED = 0, STRD_SET_SCHED, MLFQ_SCHED };

/* mlfq_lev (TOP == HIGHEST) */ 
enum mlfq_lev { TOP = 0, MID, LOW };

struct proc_strd {
    int pass;
    int stride;
    int share;          /* CPU share(%) */
};

struct proc_mlfq {
    int tick;
    enum mlfq_lev lev;  /* Level of queue */
    int priority;       /* Priority of processes on the same level */
};

struct proc_mlfqs {
    int pass;
    int stride;         /* Stride for all MLFQ scheduled processes */
    int share;          /* Share of MLFQ scheduled processes */
    int totaltick;      /* # of total ticks */
    int topn;           /* # of procs in high level queue */
    int midn;           /* # of procs in mid level queue */
    int lown;           /* # of procs in low level queue */
};

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)

  /* added properties for HW2 */
  enum sched_mode mode;
  struct proc_strd strd;
  struct proc_mlfq mlfq;

  /* added properties for HW3 */
  int lwpid;            /* LWP ID */
  int lwppid;           /* Master process ID */
  void *retval;         /* Return value of LWP */
  int sbase;            /* Stack base */
  int snum;
  int forked;
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
