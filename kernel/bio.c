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

#define NBUCKETS 13

// struct {
//   struct spinlock lock;
//   struct buf buf[NBUF];

//   // Linked list of all buffers, through prev/next.
//   // Sorted by how recently the buffer was used.
//   // head.next is most recent, head.prev is least.
//   // 对缓存块的访问都是通过bcache.head引用链表来实现的，而不是buf数组
//   struct buf head;
// } bcache;

struct {
  struct spinlock overall_lock;   //全局锁
  struct spinlock lock[NBUCKETS]; //每个哈希桶的锁
  struct buf buf[NBUF];
  struct buf hashbucket[NBUCKETS]; //每个哈希队列一个linked list及一个lock
} bcache;

char bcache_name[NBUCKETS][24];

uint 
hash (uint n) {
  return n % NBUCKETS;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.overall_lock, "bcache");

  //初始化哈希锁
  for(int i=0; i<NBUCKETS; i++){
    snprintf(bcache_name[i], sizeof(bcache_name[i]), "bcache%d", i);
    initlock(&bcache.lock[i], bcache_name[i]);
    
    //双向链表初始化 
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  }

  for(int i=0; i<NBUF; i++){
    uint h = hash(i);
    b = &bcache.buf[i];
    b->next = bcache.hashbucket[h].next;
    b->prev = &bcache.hashbucket[h];
    initsleeplock(&b->lock, "buffer");
    bcache.hashbucket[h].next->prev = b;
    bcache.hashbucket[h].next = b;
  }

  // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
  
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint key = hash(blockno);
  acquire(&bcache.lock[key]);

  // 如果在自己的哈希桶中命中
  for(b = bcache.hashbucket[key].next; b != &bcache.hashbucket[key]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 如果没命中，首先在自己的哈希桶中寻找空闲块，若有则替换后返回
  for(b = bcache.hashbucket[key].prev; b != &bcache.hashbucket[key]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 本桶没有refcnt == 0的桶，就要找别的桶，但是需要先释放本桶的锁
  release(&bcache.lock[key]);

  // 获取全局锁。仅在挪用不同哈希桶之间的内存块时使用到了全局锁，因此并不影响其余进程的并行
  acquire(&bcache.overall_lock);

  // 遍历所有哈希桶，寻找可以窃取的块
  for (int i=0; i<NBUCKETS; i++){
    if (i == key) continue;
    acquire(&bcache.lock[i]);   //获取该哈希桶的锁
    
    // 判断在其他哈希桶中是否寻找到一个空闲块
    for(b = bcache.hashbucket[i].prev; b != &bcache.hashbucket[i]; b = b->prev){
      if(b->refcnt == 0) {
        // 可以找到
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        
        // 把该块从哈希桶i中删除
        b->next->prev = b->prev;
        b->prev->next = b->next;
        
        // 将该块添加到当前哈希桶中
        acquire(&bcache.lock[key]);
        b->next = &bcache.hashbucket[key];
        b->prev = bcache.hashbucket[key].prev;
        bcache.hashbucket[key].prev->next = b;
        bcache.hashbucket[key].prev = b;
        release(&bcache.lock[key]);

        release(&bcache.lock[i]); 
        release(&bcache.overall_lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.lock[i]);
  }
  
  release(&bcache.overall_lock);
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key = hash(b->blockno);
  acquire(&bcache.lock[key]);
  b->refcnt--;
  if (b->refcnt == 0) {  
    // 没有地方再引用这个块，将块链接到缓存区链头
    // 将b从链表中删去
    b->next->prev = b->prev;
    b->prev->next = b->next;
    // 将b添加到链表头
    b->next = bcache.hashbucket[key].next;
    b->prev = &bcache.hashbucket[key];
    bcache.hashbucket[key].next->prev = b;
    bcache.hashbucket[key].next = b;

    // no one is waiting for it.
    // b->next->prev = b->prev;
    // b->prev->next = b->next;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    // bcache.head.next->prev = b;
    // bcache.head.next = b;
  }
  
  release(&bcache.lock[key]);
}

void
bpin(struct buf *b) {
  uint idx = hash(b->blockno);
  acquire(&bcache.lock[idx]);
  b->refcnt++;
  release(&bcache.lock[idx]);
}

void
bunpin(struct buf *b) {
  uint idx = hash(b->blockno);
  acquire(&bcache.lock[idx]);
  b->refcnt--;
  release(&bcache.lock[idx]);
}


