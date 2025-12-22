#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
  // 使用 E1000 发送链路帧 mbuf tx_desc tx_mbufs tx_ring
  // sys_write()，可能多线程调用
  acquire(&e1000_lock);

  // 获取E1000期望的下一个数据包的TX环索引
  // E1000_TDT 指向 tx_ring 中下一个可以被分配的 descriptor 的索引
  int index = regs[E1000_TDT];
  struct tx_desc* tail_desc = &tx_ring[index];

  // 检查环形缓冲区是否溢出，该标志位会在描述符的数据被处理完成后设置
  if((tail_desc->status & E1000_TXD_STAT_DD) == 0) {
      release(&e1000_lock);
      return -1;
  }

  // 释放之前从该描述符传输的最后一个mbuf
  if(tx_mbufs[index]) {
      mbuffree(tx_mbufs[index]);
  }

  // 设置数据包内容在内存中的地址
  tail_desc->addr = (uint64)(m->head);

  // 设置数据包长度
  tail_desc->length = m->len;

  // 设置必要的命令标志位
  // E1000_TXD_CMD_EOP: 标识这是一个数据包的结束
  // E1000_TXD_CMD_RS: 要求在完成传输后设置状态位
  tail_desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;

  // 保存mbuf指针以便后续释放
  tx_mbufs[index] = m;

  // 确保描述符的所有字段都写入内存后再更新 TDT 寄存器
  __sync_synchronize();

  // 更新E1000_TDT寄存器，指向下一个可用的描述符
  regs[E1000_TDT] = (index + 1) % TX_RING_SIZE;

  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
  // mbuf rx_desc rx_mbufs rx_ring
  while(1) {
      // 首先通过读取E1000_RDT控制寄存器并加1模RX_RING_SIZE来获取下一个等待接收的数据包所在的环索引
      // 计算下一个描述符的索引位置
      uint32 index = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
      
      // 获取对应索引的接收描述符
      struct rx_desc *des = &rx_ring[index];
      
      // 检查是否有新数据包到达，通过检查描述符状态中的E1000_RXD_STAT_DD位来判断
      // 如果没有新的数据包则返回
      if(!(des->status & E1000_RXD_STAT_DD)) {
          return;
      }

      // 更新mbuf的长度为描述符中报告的长度，并使用net_rx()将mbuf传递给网络协议栈处理
      struct mbuf *buf = rx_mbufs[index];
      buf->len = des->length;
      net_rx(buf);

      // 然后使用mbufalloc()分配一个新的mbuf来替换刚刚传递给net_rx()的那个
      struct mbuf *new_buf = mbufalloc(0);
      rx_mbufs[index] = new_buf;
      
      // 将新mbuf的数据指针(m->head)写入描述符，并将描述符的状态位清零
      des->addr = (uint64)new_buf->head;
      des->status = 0;
      
      // 确保描述符更新完成后再更新 RDT 寄存器
      __sync_synchronize();
      
      // 最后更新E1000_RDT寄存器为最后处理的环描述符索引
      regs[E1000_RDT] = index;
  }
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
