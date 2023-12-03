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

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct spinlock cowlock; // 用于cowcount数组的锁

// 物理地址p对应的物理页号
#define PAGE_INDEX(p) (((p)-KERNBASE)/PGSIZE)

// 从KERNBASE开始到PHYSTOP之间的每个物理页的引用计数（数组）
int cowcount[PAGE_INDEX(PHYSTOP)]; 

// 通过物理地址获得引用计数
#define PA_COUNT(p) cowcount[PAGE_INDEX((uint64)(p))]


void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&cowlock, "cow");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
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

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&cowlock);

  // cow_count减到零，释放内存
  if(--PA_COUNT(pa)<=0){
    memset(pa, 1, PGSIZE);
    r = (struct run*)pa;
    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }

  release(&cowlock);

}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    PA_COUNT(r) = 1;;            // 将该物理页的引用次数初始化为1
  }
  return (void*)r;
}

void
add_ref(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (uint64)pa < KERNBASE || (uint64)pa >= PHYSTOP)
    panic("add_ref");

  acquire(&cowlock);
  PA_COUNT(pa)++;
  release(&cowlock);
}


void *
cowcopy(void *pa) {
  acquire(&cowlock);

  // 当引用已经小于等于1时，不创建和复制到新的物理页，而是直接返回该页本身
  if(PA_COUNT(pa) <= 1) { 
    release(&cowlock);
    return pa;
  }

  // 分配新的内存页，并复制旧页中的数据到新页
  uint64 newpa = (uint64)kalloc();
  if(newpa == 0) {
    release(&cowlock);
    return 0; 
  }
  memmove((void*)newpa, (void*)pa, PGSIZE);

  // 旧页的引用减 1
  PA_COUNT(pa)--;

  release(&cowlock);
  return (void*)newpa;
}



