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
  return 0;  // not reached
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
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  backtrace();
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

uint64
sys_sigalarm(void)
{
  int n;          // 报警间隔（ticks数）
  uint64 p;       // 信号处理函数的用户空间地址

  // 获取系统调用参数
  argint(0, &n);      // 第一个参数：报警间隔
  argaddr(1, &p);     // 第二个参数：信号处理函数地址
  
  // 设置进程的报警参数
  myproc()->alarm_interval = n;    // 设置报警间隔
  myproc()->alarm_handler = p;     // 设置报警处理函数地址
  myproc()->intr_is_running = 0;   // 清除中断处理标志，表示当前没有信号处理程序在运行
  return 0;
}

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  
  // 恢复中断发生时保存的trapframe，这会恢复所有的用户寄存器状态
  // 包括a0寄存器
  memmove(p->trapframe, &(p->intr_trap), sizeof(struct trapframe));
  
  // 清除中断处理标志，表示信号处理已完成
  p->intr_is_running = 0;
  
  // 直接返回用户空间，不通过正常的系统调用返回路径
  // 这样可以保持所有寄存器状态，包括a0寄存器
  usertrapret();
  
  // 正常情况下不会执行到这里
  return 0;
}