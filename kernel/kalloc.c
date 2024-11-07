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
} kmem[NCPU];

void
kinit()
{
  // initlock(&kmem.lock, "kmem");
  // freerange(end, (void*)PHYSTOP);
  for(int i = 0; i < NCPU; i++){
    char name[16] = {0};
    snprintf(name, sizeof(name), "kmem - %d", i);
    initlock(&kmem[i].lock, name);
  }
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

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  
  // Fill with junk to catch dangling refs.
  push_off();
  int cpu = cpuid();
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);
  pop_off();
}

void *
ksteal(int cpu) {
  struct run *r;
  for (int i = 1; i < NCPU; i++) {
    int next_cpu = (cpu + i) % NCPU;
    // --- critical session ---
    acquire(&kmem[next_cpu].lock);
    r = kmem[next_cpu].freelist;
    if (r) {
      // steal one page
      kmem[next_cpu].freelist = r->next;
    }
    release(&kmem[next_cpu].lock);
    // --- end of critical session ---
    if (r) {
      break;
    }
  }
  // 有可能返回NULL, 如果邻居也都没有空余页的话
  return r;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int cpu = cpuid();
  acquire(&kmem[cpu].lock);
  r = kmem[cpu].freelist;
  if(r)
    kmem[cpu].freelist = r->next;
  release(&kmem[cpu].lock);

  if(r==0){
    r = ksteal(cpu);
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  pop_off();
  return (void*)r;
}
