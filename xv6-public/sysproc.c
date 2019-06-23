#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_getppid(void)
{
  return myproc()->parent->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int sys_yield(void) {
    yield();
    return 0;
}

// get the level of MLFQ running process.
int sys_getlev(void) {
    return getlev();
    //return myproc()->mlfq.lev;
    //return 0;
}

// set the cpu_share of the given process.
int sys_cpu_share(void) {
    int share;

    // argint() is used to hand over 0th the parameter
    if(argint(0, &share) < 0)
        return -1;
    return cpu_share(share);
}

// run process with mlfq scheduling
int sys_run_MLFQ(void) {
    run_MLFQ();
    return 0;
}

// create a thread
int sys_thread_create(void) {
    int thread, routine, arg;

    if((argint(0, &thread) < 0) || (argint(1, &routine) < 0) || (argint(2, &arg) < 0))
        return -1;
    return thread_create((thread_t*)thread, (void*)routine, (void*)arg);
}

// exit a thread
int sys_thread_exit(void) {
    int retval;

    if(argint(0, &retval) < 0)
        return -1;
    
    thread_exit((void*)retval);
    return 0;
}

// wait for the termination of a specific thread
int sys_thread_join(void) {
    int thread, retval;

    if((argint(0, &thread) < 0) || (argint(1, &retval) < 0))
        return -1;
    return thread_join((thread_t)thread, (void**)retval);
}
