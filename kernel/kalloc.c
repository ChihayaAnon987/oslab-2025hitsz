// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"


extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem {
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmems[NCPU];

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  
	uint64 i = 0;
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE, i++) {
    // Fill with junk to catch dangling refs.
    memset(p, 1, PGSIZE);

		// 将物理页轮流分配给不同的CPU核心
    uint64 cpu_id = i % NCPU;

		// 每个物理页作为struct run结构体加入对应CPU的空闲链表
    struct run *r = (struct run *)p;

		// 获取当前CPU的锁
    acquire(&kmems[cpu_id].lock);
    r->next = kmems[cpu_id].freelist;
    kmems[cpu_id].freelist = r;
    release(&kmems[cpu_id].lock);
  };
}


void kinit() {
  for (int i = 0; i < NCPU; i++)
    initlock(&kmems[i].lock, "kmem");
  freerange(end, (void *)PHYSTOP);
}


// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
		panic("kfree");

  push_off();
	// 获取当前CPU ID
  int cpu_id = cpuid();
  pop_off();

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

	// 将页加入当前CPU的空闲链表
  struct run *r = (struct run *)pa;

	// 获取当前CPU的锁
  acquire(&kmems[cpu_id].lock);
  r->next = kmems[cpu_id].freelist;
  kmems[cpu_id].freelist = r;
  release(&kmems[cpu_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void)
{
  push_off();
  int cpu_id = cpuid();
  pop_off();

	// 尝试从当前CPU的空闲链表分配，获取当前CPU的锁
  acquire(&kmems[cpu_id].lock);
  struct run *r = kmems[cpu_id].freelist;
  if (r) {
    kmems[cpu_id].freelist = r->next;
  } else {  // 如果当前 cpu 的 freelist 空了, 去其他 cpu 中的 freelist 中找
    for (int i = 0; i < NCPU; i++) {
      if (i == cpu_id) continue;
      acquire(&kmems[i].lock);
      r = kmems[i].freelist;
      if (r) kmems[i].freelist = r->next;
      release(&kmems[i].lock);
      if (r) break;
    }
  }
  release(&kmems[cpu_id].lock);

  if (r) memset((char *)r, 5, PGSIZE);  // fill with junk
  return (void*)r;
}
