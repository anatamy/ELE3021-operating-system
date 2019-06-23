#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"


/*
 * Operating system 2019
 * Professor: Hyungsoo Jung
 * Student NO: 2014018940
 * Student Name: Hyung-Kwon Ko
 * since: 2019-04-28
 * verison: 0.0.0
 */

/*--------------------------------------------*/
/* ADDED 2019-04-28 */
/* function declaration */
int update_strd(void);
int get_min_pass(void);
void init_pass(void);
uint get_base(struct proc* p);

/* struct for set of mlfqs  */
struct proc_mlfqs mlfqs;

int strd_share_sum = 0;
int strd_sum = 100;

/* checking the number of running processes */
int num_processes = 0;

/* checking the number of LWPs */
int num_lwp = 0;

struct st {
    int use;
};

struct {
    struct st st[NPROC*50];
} stable;
/*--------------------------------------------*/

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
int nextlwpid = 1; /* HW3 2019-05-21 */
extern void forkret(void);
extern void trapret(void);

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

/* jump here if we found UNUSED one */
found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  
  /*--------------------------------------------*/
  /* ADDED 2019-04-28 */
  /* init process*/
  memset(&p->strd, 0, sizeof(p->strd));
  memset(&p->mlfq, 0, sizeof(p->mlfq));
  p->mode = 0;
  p->lwpid = 0;
  p->lwppid = 0;
  p->retval = 0;
  p->sbase = 0;
  p->snum = 0;
  p->forked = 0;
  /*--------------------------------------------*/

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  /* now pointing the top of the stack. REFER TO COMMENTARY BOOK P.24 */
  sp = p->kstack + KSTACKSIZE; /* KSTACKSIZE = 4096 */

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

  /* the rest of the */

  /* for debugging */
  /* increase the number of processes */
  //cprintf("Total number of processes: %d\n", ++num_processes);

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
  p->sz = PGSIZE; /* 4096(4KB) */
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S
  

  /* this naming is for debugging*/
  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
  
  /* HW2 initialize mlfq set when first process is made*/
  memset(&mlfqs, 0, sizeof(mlfqs));

  /* HW3 init stable */
  for(int i=0; i<NPROC*50; i++)
      stable.st[i].use = 0;
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);

  // ** caclulate the amount of required PGSIZE
  for(int i=0; i<n/(2*PGSIZE); i++)
    get_base(curproc);
  
  // ** check whether it is called from a LWP or a normal proc
  if(curproc->lwpid > 0)
    sz = curproc->parent->sz;
  else
    sz = curproc->sz;
  
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }

  // ** resize curproc
  if(curproc->lwpid > 0)
    curproc->parent->sz = sz;
  else
    curproc->sz = sz;
  
  release(&ptable.lock);

  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  //uint sz;
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  /* if called from a LWP, set forked flag */
  if(curproc->lwpid > 0) {
    np->forked = 1;
    np->lwpid = nextlwpid++;
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

  for(i = 0; i < NOFILE; i++) // NOFILE = 16
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  
  release(&ptable.lock);
  
  /* ADDED 2019-04-28 */
  /* add info */
  //cprintf("forked\n");

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

  /* if it is a manager process */
  if(curproc->lwpid == 0) {

    /* set killed flag */
    curproc->killed = 1;

    acquire(&ptable.lock);
    
    for(;;) {
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if(p->lwppid == curproc->pid) {
                /* if a LWP is ZOMBIE, return resources */
                if(p->state == ZOMBIE) {
                    kfree(p->kstack);
                    p->kstack = 0;
                    deallocuvm(p->pgdir, p->sbase+2*PGSIZE, p->sbase);
                    p->pid = 0;
                    p->parent = 0;
                    p->name[0] = 0;
                    p->killed = 0;
                    p->state = UNUSED;

                    p->lwpid = 0;
                    p->lwppid = 0;
                    p->sbase = 0;
                    p->snum = 0;
                    p->forked = 0;
                    p->retval = 0;

                    num_lwp--; /* number of lwps-- */
                
                } else {
                    p->killed = 1; /* set killed flag of a LWP */
                    wakeup1(p);
                }
            }
        }
        
        if(num_lwp == 0) {
            release(&ptable.lock);
            break;
        }
        sleep(curproc, &ptable.lock);
    }
  } else if(curproc->forked != 1) {
    /* if called from a LWP, kill master proc */
    curproc->parent->killed = 1;
  }

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

  // Parent might be sleeping in wait().
  /* if curproc is a manager */
  if(curproc->lwpid == 0) {
    wakeup1(curproc->parent);
  } else {
    /* if curproc is LWP && manager is active */
    if(curproc->parent != 0) {
      wakeup1(curproc->parent);
    } 
  }
  
  /* ADDED 2019-04-28 */
  /* we need to readjust share when STRD_SET_SCHED ends */
  if(curproc->mode == STRD_SET_SCHED) {
    strd_sum += curproc->strd.share;
    strd_share_sum -= curproc->strd.share;
  }

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }
  
  /* ADDED 2019-04-28 */
  /* for debugging */
  /*if(curproc->mode == STRD_SCHED) {
    cprintf("STRD_SCHED END\n");
  } else if(curproc->mode == STRD_SET_SCHED) {
    cprintf("STRD_SET_SCHED END\n");
  } else if(curproc->mode == MLFQ_SCHED) {
    cprintf("MLFQ_SCHED END\n");
  }*/

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
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

/* ADDED 2019-04-28 */
/* get the number of processes with the matching mode */
int get_num(enum sched_mode sm) {
    int num = 0;
    struct proc *p = 0;

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(p->state != RUNNABLE)
        continue;
      if(p->mode == sm)
        num++;
    }
    
    return num;
}

/* recalculate strides for all processes */
int update_strd(void) {

    int n1 = get_num(STRD_SCHED);
    int n2 = get_num(MLFQ_SCHED);
    //int n3 = get_num(STRD_SET_SCHED); // unnecessary

    struct proc *p = 0;

    /* for all RUNNABLE proceeses */
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      
      switch(p->mode) {
        case STRD_SCHED:
          p->strd.share = strd_sum / n1;
          p->strd.stride = 10001 / p->strd.share;
          break;
        case STRD_SET_SCHED:
          p->strd.stride = 10001 / p->strd.share;
          break;
        case MLFQ_SCHED:
          if(n2 <= 0) {
            p->strd.share = 0; // for implicity 
            p->strd.stride = 0; // for implicity
            mlfqs.share = 0; // for implicity
            break;
          } else {
            /* share is divided by # of MLFQ scheduled processes*/
            p->strd.share = mlfqs.share / n2;
            p->strd.stride = 10001 / p->strd.share;
            //p->mlfq.stride = 10001 / (p->mlfq.share * n2);
            break;
          }
      }
    }

    return 0;
}

/* get the level of mlfq running process */
int getlev(void) {
    return myproc()->mlfq.lev;
}

/* get the minimum pass among all processes */
int get_min_pass(void) {
    
    int min = 99999999;
    struct proc *p;

    acquire(&ptable.lock);

    /* find minimum pass among STRD running process */
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      
      if(p->mode == STRD_SCHED || p->mode == STRD_SET_SCHED)
        if(min > p->strd.pass)
          min = p->strd.pass;
    }
    
    release(&ptable.lock);

    /* if mlfq running processes exist, compare with them */
    if(get_num(MLFQ_SCHED) > 0)
      if(min > mlfqs.pass)
        min = mlfqs.pass;

    return min; 
}

/* 1. set sched_mode to STRD_SET_SCHED/STRD_SCHED
 * 2. set share of the process
 * 3. update stride for all processes
 * 4. initialize pass for all processes */
int cpu_share(int share) {
   
    struct proc *p = myproc();
    int n, m;

    acquire(&ptable.lock);
    
    if(strd_share_sum < 0 || share < 0) {
        panic("share_sum < 0 || share < 0");
        return -1;
    }
   
    strd_share_sum += share;
    strd_sum -= share;
    p->mode = STRD_SET_SCHED;
    p->strd.share = share;
   
    /* code for exception handling */
    if(strd_share_sum > 20) {
        /* roll back */
        strd_share_sum -= share;
        strd_sum += share;
        p->mode = STRD_SCHED;
        
        /* if there is a mlfq running process, (80 - x) / n */
        /* else (100 - x) / n */
        /* x = shares of STRD_SET_SCHED running processes */
        n = get_num(MLFQ_SCHED);
        m = get_num(STRD_SCHED);
        
        if(n > 0) {
            p->strd.share = (80 - strd_share_sum) / m;    
        } else if(n == 0) {
            p->strd.share = (100 - strd_share_sum) / m;
        } else {
            panic("# of MLFQ running process < 0\n");
            return -1;
        }
       
        /* update stride for every process*/
        update_strd();
        /* initialize all pass */
        init_pass();
        
        release(&ptable.lock);
        return 1;
    }

    /* update stride for every process*/
    update_strd();
    /* initialize all pass */
    init_pass();
    
    release(&ptable.lock);
    return 0;  
}

/* 1. set sched_mode to MLFQ_SCHED
 * 2. initialize variables
 * 3. recalculate strides for MLFQ running processes
 * 4. initialize pass of all processes */
int run_MLFQ(void) {
    
    struct proc *p = myproc();
    
    /* if first process by MLFQ, set information */
    acquire(&ptable.lock);
    
    if(mlfqs.share == 0) {
        mlfqs.share = 20;
        mlfqs.stride = 10001/20;
        mlfqs.pass = 0;
        //mlfqs.pass = get_min_pass();
        strd_sum -= 20;
    }
  
    /* add it to the top level*/
    mlfqs.topn++;

    /* basic set up for MLFQ running process */
    p->mode = MLFQ_SCHED;
    p->mlfq.lev = TOP;
    p->mlfq.priority = 0;
   
    /* update stride for each process */
    update_strd();

    /* initialize pass for each process */
    init_pass(); 
    
    release(&ptable.lock);
    
    return 0;
}

/* code to initialize the pass of all processes*/
void init_pass(void) {
  
  struct proc *p;

  /* make pass of all processes = 0 */
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state != RUNNABLE)
      continue;
    p->strd.pass = 0;
  }
  
  /* make pass of mlfqs struct = 0 */
  mlfqs.pass = 0;
}

void
scheduler(void) {
    struct proc *p;
    struct cpu *c = mycpu();
    c->proc = 0;
    for(;;) {
        sti();
        acquire(&ptable.lock);
        for(p=ptable.proc; p<&ptable.proc[NPROC]; p++) {
            if(p->state != RUNNABLE)
                continue;
            c->proc = p;
            switchuvm(p);
            p->state = RUNNING;
            swtch(&(c->scheduler), p->context);
            switchkvm();
            c->proc = 0;
        }
        release(&ptable.lock);
    }
}

/*
void
scheduler(void)
{
  struct proc *p;
  struct proc *tproc; // temp process
  struct cpu *c = mycpu();
  int min; // min pass
  int tpri; // temp priority
  int m; // # of mlfq running process
  enum mlfq_lev tlev = LOW; // top level of mlfq running process
 
  c->proc = 0;
  tproc = 0;

  for(;;) {
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    
    min = 999999999;

    // loop over STRD or STRD_SET scheduled process first/
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(p->state != RUNNABLE || p->mode == MLFQ_SCHED)
        continue;

      if(min >= p->strd.pass) {
        min = p->strd.pass;
        tproc = p;
      }
    }

    // get the # of MLFQ running processes /
    m = get_num(MLFQ_SCHED);

    // run STRIDE scheduled process /
    if((m == 0) || (m > 0 && min < mlfqs.pass)) {
      p = tproc;
      c->proc = p;
      
      // switch process /
      switchuvm(p);
      p->state = RUNNING;
      
      swtch(&c->scheduler, p->context);
      switchkvm();

      // add stride to the pass /
      p->strd.pass += p->strd.stride;
    
    // run MLFQ scheduled process /
    } else if(m > 0 && min >= mlfqs.pass) {
      
      min = mlfqs.pass;
      tpri = 999999999;
      tlev = LOW;
      
      // loop over and find process with top level and high priority /
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if((p->state != RUNNABLE) || (p->mode != MLFQ_SCHED))
          continue;
        
        // updae temp variables /
        if(p->mlfq.lev <= tlev) {
          if(p->mlfq.priority <= tpri) {
            tlev = p->mlfq.lev;
            tpri = p->mlfq.priority;
            tproc = p;
          }
        }
      }
        
      // switch process /
      p = tproc;
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      
      swtch(&c->scheduler, p->context);
      switchkvm();

      // update per-process info /
      if(p->mode == MLFQ_SCHED) {
        p->strd.pass += p->strd.stride;
        p->mlfq.tick++;
        
        mlfqs.pass += (p->strd.stride / m);
        mlfqs.totaltick++;
      } else {
        panic("p->mode != MLFQ_SCHED\n");
      }

      // per-process MLFQ level info update /
      if(p->mlfq.lev == TOP) {
        // TOP level queue time allotment check /
        if(p->mlfq.tick >= TOP_ALLOT) {
          p->mlfq.lev = MID;
          p->mlfq.tick = 0;
          mlfqs.topn--;
          mlfqs.midn++;
        // TOP level queue time quanta check /
        } else if(p->mlfq.tick % TOP_QUANT == 0) {
          p->mlfq.priority++;
        }
      } else if(p->mlfq.lev == MID) {
        // MID level queue time allotment check /
        if(p->mlfq.tick >= MID_ALLOT) {
          p->mlfq.lev = LOW;
          p->mlfq.tick = 0;
          mlfqs.midn--;
          mlfqs.lown++;
        // MID level queue time quanta check /
        } else if(p->mlfq.tick % MID_QUANT == 0) {
          p->mlfq.priority++;
        }
      } else if(p->mlfq.lev == LOW) {
        // LOW level queue time quanta check /
        if(p->mlfq.tick % LOW_QUANT == 0) {
          p->mlfq.priority++;
        }
      } else {
        panic("p->mlfq.lev == ?\n");
      }

      // BOOSTING /
      if(mlfqs.totaltick >= BOOSTICK) {
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
          if((p->state != RUNNABLE) || (p->mode != MLFQ_SCHED))
            continue;
          p->mlfq.tick = 0;
          p->mlfq.priority = 0;
          p->mlfq.lev = TOP;
        }
        mlfqs.totaltick = 0;
        mlfqs.lown = 0;
        mlfqs.midn = 0;
        mlfqs.topn = m;

        // for debugging /
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
          if((p->state == RUNNABLE) || (p->mode = RUNNING)) {
            if(p->mode == MLFQ_SCHED) {
              cprintf("pid: %d, pmode: %d, pshare: %d, pstride: %d, ppass: %d\n",p->pid, p->mode, p->mlfq.share, p->mlfq.stride, p->mlfq.pass);
            } else {
              cprintf("pid: %d, pmode: %d, sshare: %d, sstride: %d, spass: %d\n",p->pid, p->mode, p->strd.share, p->strd.stride, p->strd.pass);
            }
          }
        }
      } 
    } else {
      panic("get_num(MLFQ_SCHED) < 0\n");
    }
   
    // update stride of all processes /
    update_strd();

    // initialize all pass to 0 if anomaly is detected /
    if(c->proc->strd.pass > 10000000) {
      init_pass();
    }
    
    c->proc = 0;
    
    release(&ptable.lock);
  }
}*/

/* HW3 ADDED 2019-05-21 */
uint get_base(struct proc* p) {
    
    for(uint i=0; i<NPROC*50; i++) {
        if(stable.st[i].use != 0)
            continue;
        
        /* set it as being used by proc */
        stable.st[i].use = 1;
        p->snum = i;

        /* return i */
        return i;
    }
    panic("get_base() panic");
}

/* function to generate a new LWP */
int thread_create(thread_t* thread, void* (*start_routine)(void*), void* arg) {
    int i;
    struct proc *np;
    struct proc *curproc = myproc();

    /* allocate process(LWP) */
    if((np = allocproc()) == 0) {
        return -1;
    }
    
    /* increase the total # of LWP */
    num_lwp++;
    
    /* acquire ptable lock */
    acquire(&ptable.lock);

    if(curproc->lwpid > 0) { /* when curproc = LWP */
        np->lwppid = curproc->lwppid;
        np->parent = curproc->parent;
    } else if(curproc->lwpid == 0){ /* when curproc != LWP */
        np->lwppid = curproc->pid;
        np->parent = curproc;
    } else {
        cprintf("curproc->lwpid < 0");
        return -1;
    }

    /* assign LWP id (like pid) */
    np->lwpid = nextlwpid++;
    *thread = np->lwpid;
   
    /* assign other resources */
    np->pgdir = curproc->pgdir;
    *np->tf = *curproc->tf;
    safestrcpy(np->name, curproc->name, sizeof(curproc->name));
    np->cwd = idup(curproc->cwd);

    /* set stack base(top of the thread stack) w/ function get_base() */
    np->sbase = curproc->sz + (uint)(2*PGSIZE*(get_base(np)+1));

    /* if allocation failed, return -1 */
    if((np->sz = allocuvm(np->pgdir, np->sbase, np->sbase + 2*PGSIZE)) == 0) {
        np->state = UNUSED;
        return -1;
    } 
    
    /* set esp of the LWP */
    np->tf->esp = np->sz - 4;
    *((uint*)(np->tf->esp)) = (uint)arg;
    np->tf->esp = np->sz - 8;
    *((uint*)(np->tf->esp)) = 0xffffffff;
    
    /* set eip of the LWP */
    np->tf->eip = (uint)start_routine;

    for(i=0; i<NOFILE; i++)
        if(curproc->ofile[i])
            np->ofile[i] = filedup(curproc->ofile[i]);

    /* change its status to RUNNABLE */
    np->state = RUNNABLE;
    release(&ptable.lock);

    //cprintf("finish thread_create\n");
    
    return 0;
}

/* function to set the LWP's status as ZOMBIE */
void thread_exit(void* retval) {
    
    struct proc *curproc = myproc();
    int fd;

    /* initproc should not exit */
    if(curproc == initproc)
        panic("init exiting\n");

    for(fd=0; fd<NOFILE; fd++) {
        if(curproc->ofile[fd]) {
            fileclose(curproc->ofile[fd]);
            curproc->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(curproc->cwd);
    end_op();
    curproc->cwd = 0;

    acquire(&ptable.lock);

    /* set return value for LWP */
    curproc->retval = retval;
    
    /* wakeup parent(manager) process */
    wakeup1(curproc->parent);

    /* append all ZOMBIE procs to init proc
    for(p = ptable.proc; p<&ptable.proc[NPROC]; p++) {
        if(p->parent == curproc) {
            p->parent = initproc;
            if(p->state == ZOMBIE)
                wakeup1(initproc);
        }
    }*/
   
    curproc->state = ZOMBIE; 
    num_lwp--;

    sched();
    
    /* for debugging */
    cprintf("curproc->state: %d\n", curproc->state);
    cprintf("curproc->lwppid: %d\n", curproc->lwppid);
    cprintf("curproc->lwpid: %d\n", curproc->lwpid);
    cprintf("curproc->pid: %d\n", curproc->pid);
    cprintf("num_lwp: %d\n", num_lwp);
    
    panic("zombie exit\n");
}

/* function to return the resources allocated to the LWP */
int thread_join(thread_t thread, void** retval) {
    
    struct proc *p;
    struct proc *curproc = myproc();
    
    /* error if current proc is not a manager */
    if(curproc->lwpid != 0) {
        cprintf("curproc not a manager\n");
        return -1;
    }

    acquire(&ptable.lock);
    
    for(;;) {
        for(p=ptable.proc; p<&ptable.proc[NPROC]; p++) {
            /* skip if it is not the process we want */
            if(p->lwpid != thread)
                continue;
            
            if(p->lwppid != curproc->pid)
                continue;
            
            /* for debugging */
            //cprintf("skip\n");

            if(p->state == ZOMBIE) {

                /* set return value */
                *retval = p->retval;
                
                /* free up basic resources */
                kfree(p->kstack);
                p->kstack = 0;
                deallocuvm(p->pgdir, p->sbase+2*PGSIZE, p->sbase);
                
                /* set get_base regarding vars */
                stable.st[p->snum].use = 0;
                
                p->pid = 0;
                p->parent = 0;
                p->name[0] = 0;
                p->killed = 0;
                p->state = UNUSED;

                /* free up added properties */
                p->lwpid = 0;
                p->lwppid = 0;
                p->sbase = 0;
                p->snum = 0;
                p->forked = 0;
                p->retval = 0;

                release(&ptable.lock);
                
                return 0;
            }
        }
        
        if(curproc->killed) {
            release(&ptable.lock);
            return -1;
        }
        sleep(curproc, &ptable.lock);
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
    iinit(ROOTDEV); /* ROOTDEV = 1 */
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
  struct proc *curproc = myproc();

  acquire(&ptable.lock);

  // ** kill all LWPs
  if(curproc->lwpid > 0){
    for(p=ptable.proc; p<&ptable.proc[NPROC]; p++){
      if(p->lwpid > 0) {
        p->killed = 1;
        if(p->state == SLEEPING)
          p->state = RUNNABLE;
      }
    }
    release(&ptable.lock);
    return 0;
  } else {
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

void
lower_num_lwp(void)
{
  num_lwp--;
}

// ** sleep other LWPs and set current LWP as a normal proc
// so it can properly execute designated action
void
sleep_others(void)
{
  struct proc *p;

  acquire(&ptable.lock);

  if(myproc()->killed){
    release(&ptable.lock);
    return;
  }

  for(p=ptable.proc; &ptable.proc[NPROC]; p++){
    if(p->lwppid == 0)
      continue;
    p->killed = 1;
    //p->state = SLEEPING;
  }

  release(&ptable.lock);
}

