#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

// 添加对proc数组的外部声明
extern struct proc proc[];
uint64 sys_yield(void) {
  // 获取当前CPU正在运行的进程
  struct proc *p = mycpu()->proc;

  // 打印进程的内核线程上下文被保存的地址范围
  // 打印当前进程的pid和此进程在用户态的pc值，也就是陷入内核的那条指令地址，即ecall指令的地址
  printf("Save the context of the process to the memory region from address %p to %p\n", (uint64)&p->context, (uint64)&p->context + sizeof(struct context));
  printf("Current running process pid is %d and user pc is %p\n", p->pid, p->trapframe->epc);
  
  // 计算当前进程在进程数组中的下一个进程索引
  int i = p - proc + 1;
  while (1) {
    // 回到当前进程，则跳出循环
    if (i == p - proc) {
      break;
    }
    if (proc[i].state == RUNNABLE) {
      // 打印即将被调度到的进程的pid和此时用户态的pc值
      printf("Next runnable process pid is %d and user pc is %p\n", proc[i].pid, proc[i].trapframe->epc);
      break;
    }
    i = (i + 1) % NPROC;
  }
  // 调用yield函数主动让出CPU，触发进程调度
  yield();
  return 0;
}

uint64 sys_exit(void) {
  int n;
  if (argint(0, &n) < 0) return -1;
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_wait(void) {
  uint64 p;
  if (argaddr(0, &p) < 0) return -1;
  int flags;
  if (argint(1, &flags) < 0) return -1;
  return wait(p, flags);
}

uint64 sys_sbrk(void) {
  int addr;
  int n;

  if (argint(0, &n) < 0) return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0) return -1;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  uint ticks0;

  if (argint(0, &n) < 0) return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  if (argint(0, &pid) < 0) return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_rename(void) {
  char name[16];
  int len = argstr(0, name, MAXPATH);
  if (len < 0) {
    return -1;
  }
  struct proc *p = myproc();
  memmove(p->name, name, len);
  p->name[len] = '\0';
  return 0;
}
