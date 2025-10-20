// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13

typedef struct buf buf_t;

// 定义最小堆数据结构，用于管理空闲缓冲区列表。通过时间戳来确定哪个缓冲区是最久未使用的。
typedef struct {
  uint timestamp;
  buf_t *pointer;
} entry_t;
typedef struct {
  int size;
  entry_t array[NBUF];
} MinHeap;

typedef struct {
  struct spinlock lock;    // 锁
  buf_t head;              // 缓冲区链表头节点
} bucket_t;

struct {
  struct spinlock freelock;   // 空闲链表锁
  MinHeap freelist;           // 空闲链表(最小堆实现)
  buf_t buf[NBUF];            // 所有缓冲区
  bucket_t buckets[NBUCKET];  // 哈希表桶
} bcache;


// 交换两个 entry_t 的值
void swap(entry_t *x, entry_t *y) {
  entry_t temp = *x;
  *x = *y;
  *y = temp;
}

void heapifyUp(MinHeap *heap, int idx) {
  int parent = (idx - 1) / 2;
  if (idx && heap->array[parent].timestamp > heap->array[idx].timestamp) {
    swap(&heap->array[parent], &heap->array[idx]);
    heapifyUp(heap, parent);
  }
}

void heapifyDown(MinHeap *heap, int idx) {
  int left = 2 * idx + 1;
  int right = 2 * idx + 2;
  int smallest = idx;
  if (left < heap->size && heap->array[left].timestamp < heap->array[smallest].timestamp) smallest = left;
  if (right < heap->size && heap->array[right].timestamp < heap->array[smallest].timestamp) smallest = right;
  if (smallest != idx) {
    swap(&heap->array[smallest], &heap->array[idx]);
    heapifyDown(heap, smallest);
  }
}

void insertMinHeap(MinHeap *heap, entry_t key) {
  if (heap->size == NBUF) panic("heap full\n");
  heap->array[heap->size] = key;
  heap->size++;
  heapifyUp(heap, heap->size - 1);
}

entry_t extractMin(MinHeap *heap) {
  if (heap->size <= 0) {
    entry_t empty = {0xffffffff, 0};
    return empty;
  }
  if (heap->size == 1) {
    heap->size--;
    return heap->array[0];
  }
  entry_t root = heap->array[0];
  heap->array[0] = heap->array[heap->size - 1];
  heap->size--;
  heapifyDown(heap, 0);
  return root;
}

entry_t getMin(MinHeap *heap) {
  if (heap->size <= 0) {
    entry_t empty = {0xffffffff, 0};
    return empty;
  }
  return heap->array[0];
}

void binit(void) {
  // 初始化空闲链表锁
  initlock(&bcache.freelock, "freelock");

  bcache.freelist.size = 0;
  
  // 为每个bucket初始化独立的锁
  for (int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.buckets[i].lock, "bucket");
    bcache.buckets[i].head.next = 0;
    bcache.buckets[i].head.prev = 0;
  }

  // Create linked list of buffers
  for (int i = 0; i < NBUF; i++) {
    initsleeplock(&bcache.buf[i].lock, "buffer");
    entry_t entry = {0, &bcache.buf[i]};
    insertMinHeap(&bcache.freelist, entry);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *bget(uint dev, uint blockno) {
  // 使用blockno对NBUCKET取模得到hash值，定位到特定的bucket
  int hash = blockno % NBUCKET;
  // 获取对应bucket的锁
  acquire(&bcache.buckets[hash].lock);
  // 在该bucket的链表中搜索目标缓冲区
  for (buf_t *cur = bcache.buckets[hash].head.next; cur; cur = cur->next) {
    if (cur->dev == dev && cur->blockno == blockno) {
      cur->refcnt++;
      cur->timestamp = ticks;
      // 找到, 解锁, 返回
      release(&bcache.buckets[hash].lock);
      acquiresleep(&cur->lock);
      return cur;
    }
  }

retry:
  // 获取专门保护空闲列表的锁freelock
  acquire(&bcache.freelock);
  // 从空闲列表中选择一个最久未使用的缓冲区
  entry_t iter = getMin(&bcache.freelist);
  if (iter.pointer == 0) {
    // 没有空闲缓冲区, 释放锁, 重试
    release(&bcache.freelock);
    goto retry;
  }

  iter = extractMin(&bcache.freelist);
  buf_t *cur = iter.pointer;

  // pop cur from free
  release(&bcache.freelock);

  // 初始化新分配的缓冲区并将其添加到对应bucket的链表头部
  cur->dev = dev;  // init
  cur->blockno = blockno;
  cur->valid = 0;
  cur->refcnt = 1;
  cur->timestamp = ticks;

  // push cur to head
  cur->next = bcache.buckets[hash].head.next;
  cur->prev = &bcache.buckets[hash].head;
  bcache.buckets[hash].head.next = cur;
  if (cur->next) {
    cur->next->prev = cur;
  }
  // 释放bucket锁并获取缓冲区的睡眠锁后返回
  release(&bcache.buckets[hash].lock);
  acquiresleep(&cur->lock);
  return cur;

  // failed
  release(&bcache.freelock);
  release(&bcache.buckets[hash].lock);
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *bread(uint dev, uint blockno) {
  struct buf *b = bget(dev, blockno);
  if (!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b) {
  if (!holdingsleep(&b->lock)) panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b) {
  if (!holdingsleep(&b->lock)) panic("brelse");

  int hash = b->blockno % NBUCKET;

  releasesleep(&b->lock);

  acquire(&bcache.buckets[hash].lock);
  b->refcnt--;
  // 如果引用计数变为0，将缓冲区从bucket链表中移除
  if (b->refcnt == 0) {
    if (b->next) {
      b->next->prev = b->prev;
    }
    b->prev->next = b->next;

    acquire(&bcache.freelock);
    entry_t entry = {ticks, b};
    b->next = 0;
    b->prev = 0;
    insertMinHeap(&bcache.freelist, entry);
    release(&bcache.freelock);
  }
  release(&bcache.buckets[hash].lock);
}

// 增加缓冲区的引用计数
void bpin(struct buf *b) {
  acquire(&bcache.buckets[b->blockno % NBUCKET].lock);
  b->refcnt++;
  release(&bcache.buckets[b->blockno % NBUCKET].lock);
}

// 减少缓冲区的引用计数
void bunpin(struct buf *b) {
  acquire(&bcache.buckets[b->blockno % NBUCKET].lock);
  b->refcnt--;
  release(&bcache.buckets[b->blockno % NBUCKET].lock);
}