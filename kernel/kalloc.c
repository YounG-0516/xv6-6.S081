// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

// run结构体就是一个指向自身的指针，用于指向下一个空闲页表开始位置
struct run {
  struct run *next;
};

// 管理物理内存的结构,有一把锁lock保证访问时的互斥性,以及一个指向
struct kmem{
  struct spinlock lock;
  struct run *freelist;
};

// 为每个CPU独立的freelist
struct kmem kmems[NCPU];

char lock_name[NCPU][24];

void
kinit()
{
  for(int i=0; i<NCPU; i++){
    snprintf(lock_name[i], sizeof(lock_name[i]), "kmem%d", i);
    initlock(&kmems[i].lock, lock_name[i]);
  }
  freerange(end, (void*)PHYSTOP);
}

// 这个函数用来以页为单位释放[pa_start, pa_end]这个范围内的物理内存。
// pa_start和pa_end函数不一定要是完全页对齐的，这个函数首先会使用PGROUNDUP宏将页面强制对齐，然后逐页释放到终点页面。
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  // 向上对齐，防止释放有用的页面
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  // 如果要释放的内存不是页对齐的，或者不在自由内存范围内，陷入panic
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // acquire(&kmem.lock);
  // r->next = kmem.freelist;
  // kmem.freelist = r;
  // release(&kmem.lock);
  
  // 获取当前CPU编号
  push_off();
  int cpu_id = cpuid();
  pop_off();

  // 将对应的空闲页放入空闲列表中
  acquire(&kmems[cpu_id].lock);
  r->next = kmems[cpu_id].freelist;
  kmems[cpu_id].freelist = r;
  release(&kmems[cpu_id].lock);
  
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // 获取当前CPU编号
  push_off();
  int cpu_id = cpuid();
  pop_off();

  acquire(&kmems[cpu_id].lock);
  r = kmems[cpu_id].freelist;

  //如果当前CPU有空闲内存块
  if(r) {
    kmems[cpu_id].freelist = r->next;
    release(&kmems[cpu_id].lock);
  }
  //如果当前CPU没有空闲内存块
  else {
    release(&kmems[cpu_id].lock);
    for(int i=0; i<NCPU; i++) {
      if (i == cpu_id)  continue;

      acquire(&kmems[i].lock);
      r = kmems[i].freelist;
      if (r) {
        kmems[i].freelist = r->next;
        release(&kmems[i].lock);
        break;
      } else {
        release(&kmems[i].lock);
      }
      
      // 所有CPU都没有空闲块时，返回0
      if(i == NCPU-1)  return 0;
    }
  }
      
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
