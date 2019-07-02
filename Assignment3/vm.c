#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P){
      panic("remap");
    }
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();  
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;  

  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
 
  lcr3(V2P(p->pgdir));  // switch to process's address space  
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

int
allocuvm_paging_swapout(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;
  long long time_counter;
  pte_t *pte;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;
  // check number of pages for current process
  // if the process have place in pysical memory then create page -insert to IN pages
  // else create metadata and create pte for va -insert to OUT pages
  struct proc * p = myproc();
  a = PGROUNDUP(oldsz);  
  for(; a < newsz; a += PGSIZE){    
    if(get_pages_count(p->pages_IN) < MAX_PSYC_PAGES ){
      mem = kalloc();
      if(mem == 0){
        cprintf("allocuvm out of memory\n");
        deallocuvm(pgdir, newsz, oldsz);
        return 0;
      }
      memset(mem, 0, PGSIZE);
      if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_P |PTE_W |PTE_U) < 0){
        cprintf("allocuvm out of memory (2)\n");
        deallocuvm(pgdir, newsz, oldsz);
        kfree(mem);
        return 0;
      }      
      // update upages for new page meta data      
      time_counter = add_to_upages(p->pages_IN,(void*)a,p->time_load_counter);
      if(time_counter < 0){        
        deallocuvm(pgdir, newsz, oldsz);
        kfree(mem);
        return 0;
      }
      else{
        myproc()->time_load_counter = time_counter;
      }     
    }
    else{       
      time_counter = add_to_upages(p->pages_OUT, (void*)a, p->time_load_counter);
      if(time_counter < 0){
        cprintf("allocuvm: couldn't place page OUT upages\n");
        return 0;
      }  
   
      if(mappages(pgdir, (char*)a, PGSIZE, 0, PTE_PG | PTE_W | PTE_U) < 0){
        cprintf("allocuvm: mappages failed\n");
        return 0;
      }
      // remove PTE_P flag from pte
      pte = walkpgdir(pgdir, (void *) a, 0);
      *pte &= ~PTE_P;
      swap_page_IN((void*)a, pgdir);
    }
  }
 

  return newsz;
}

int
allocuvm_NONE(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  #ifdef NONE
    return allocuvm_NONE(pgdir, oldsz, newsz);
  #endif
  return allocuvm_paging_swapout(pgdir, oldsz, newsz);  
}

int
deallocuvm_paging_swapout(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;
  struct page_data* pages_meta_data =0;
  struct page_data *pd;
  struct proc * p = myproc();

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);  
  for(; a  < oldsz; a += PGSIZE){
    int found_indicator = 0;
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      found_indicator = 1;
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);      
      pages_meta_data = p->pages_IN;
      pd = get_page_data(pages_meta_data,(void*)a);
      if(pd!=0){     
      // if located IN memory need to free memory and meta data
        kfree(v);      
      }
    }
    else if((*pte & PTE_PG) != 0){
      found_indicator = 1;      
      pages_meta_data = p->pages_OUT;
      pd = get_page_data(pages_meta_data,(void*)a); 
      if(pd->fileOffset > -1){
      update_min_swapfile_offset(pd->fileOffset);
      }
    }  
    if(found_indicator){
      // update meta data       
      *pte = 0; 
      if(update_upages(pages_meta_data, (void*)a)<0){
        panic("deallocuvm: update_upages failed");
      }   
    }            
  }  
  return newsz;
}

int
deallocuvm_NONE(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}


// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  #ifdef NONE
    return deallocuvm_NONE(pgdir, oldsz, newsz);
  #endif
  return deallocuvm_paging_swapout(pgdir, oldsz, newsz); 
} 

  

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;
  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){    
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

pde_t*
copyuvm_NONE(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0)
      goto bad;
  }
  return d;

bad:
  freevm(d);
  return 0;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm_paging_swapout(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  // pte_t *d_pte;
  uint pa, i, flags;
  char *mem;
  uint pmem;  

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P) && !(*pte & PTE_PG))
      panic("copyuvm: page not present");

    int PTE_PG_indicator = 0;
    if(*pte & PTE_PG){
      PTE_PG_indicator=1;
    }   
    flags = PTE_FLAGS(*pte);
    if(PTE_PG_indicator == 0){
      pa = PTE_ADDR(*pte);
      if((mem = kalloc()) == 0)
      goto bad;
      memmove(mem, (char*)P2V(pa), PGSIZE);
      pmem = V2P(mem);      
    }
    else{
      pmem = 0;      
    } 
    if(mappages(d, (void*)i, PGSIZE, pmem, flags) < 0)
      goto bad; 

    if(PTE_PG_indicator == 1){
      pte_t * d_pte = walkpgdir(d,(void*)i, 0);
      if(*d_pte & PTE_PG){
        *d_pte &= ~PTE_P;
      }     
    } 
  }
  return d;

bad:
  freevm(d);
  return 0;
}

pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  #ifdef NONE
    return copyuvm_NONE(pgdir, sz);
  #endif
  return copyuvm_paging_swapout(pgdir, sz);  
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

int 
check_valid_flag(uint flag){
  if(flag == PTE_PR || flag == PTE_W || flag == PTE_PG || flag == PTE_A || flag == PTE_U || flag == PTE_P){
    return 1;
  }
  return 0;
} 


int
add_flag_to_pte(uint flag, void* va){
  if (check_valid_flag(flag)){
    struct proc *p = myproc();
    pde_t* pgdir = p->pgdir;
    pte_t * pte = walkpgdir(pgdir,va, 0);   
    if(*pte & PTE_P || *pte & PTE_PG){ 
      *pte = *pte | flag; 
      return 0;
    }
  }  
  // not valid flag for add or not pte
  return -1;  
}

int 
remove_flag_from_pte(uint flag, void* va){
  if (check_valid_flag(flag)){
    struct proc *p = myproc();
    pde_t* pgdir = p->pgdir;
    pte_t * pte = walkpgdir(pgdir,va, 0);
    if(*pte & PTE_P || *pte & PTE_PG){ 
      // check that the removed flag was on the flags before
      *pte = *pte & ~flag;
      return 0;
    }
  }  
  // not valid flag for add or not pte
  return -1; 
}

int 
check_flag_on_pte(uint flag, void* va){
  if (check_valid_flag(flag)){
    struct proc *p = myproc();
    pde_t* pgdir = p->pgdir;
    pte_t * pte = walkpgdir(pgdir,va, 0);  

    if(*pte & PTE_P || *pte & PTE_PG){
      if(*pte & flag){
        return 0;
      }
    }
    else{
      cprintf("in check_flag_on_pte pte not PTE_P or PTE_PG va: 0x%x, *pte:  0x%x, flag: %x\n", va, *pte, flag);
    }
  }
  return -1;
}

struct page_data* 
execute_LIFO(void){
  struct proc* p = myproc();
  struct page_data* pd;
  long long max_loaded_time = 0;
  struct page_data* chosen_pd = 0;
  for(pd = p->pages_IN ; pd < &p->pages_IN[MAX_PSYC_PAGES]; pd++){    
    if(pd->used && (pd->load_time > max_loaded_time)){
      chosen_pd = pd;
      max_loaded_time = pd->load_time;      
    }
  }  
  // init values in index
  chosen_pd->used = 0;
  void* va = chosen_pd->va; 
  chosen_pd->va = 0;
  return va;  
}

struct page_data*
get_pd_with_min_loaded_time(struct page_data* pages_IN){
  struct page_data* pd;
  long long min_loaded_time = pages_IN->load_time;
  struct page_data* chosen_pd = pages_IN;

  for(pd = pages_IN ; pd < &pages_IN[MAX_PSYC_PAGES]; pd++){    
    if(pd->used && (pd->load_time < min_loaded_time)){
      chosen_pd = pd;
      min_loaded_time = pd->load_time;      
    }
  } 

  return chosen_pd;
}

struct page_data* 
execute_SCFIFO(pde_t* pgdir){
  struct proc* p = myproc();
  pte_t * pte;
  struct page_data* pd = get_pd_with_min_loaded_time(p->pages_IN);

  while (1){
    pte = walkpgdir(pgdir, pd->va, 0);
    if(*pte & PTE_A){
      pd->load_time = p->time_load_counter;
      p->time_load_counter += 1;
      // need to remove flag from pte PTE_A
      if(*pte & PTE_P || *pte & PTE_PG){ 
        *pte &= ~PTE_A;
      }
      else{
        cprintf("execute_SCFIFO: failed to remove PTE_A flag\n");
      }      
    }
    else{
      goto found;
    }
    pd = get_pd_with_min_loaded_time(p->pages_IN);
  }
found:
  // init values in index
  pd->used = 0;  
  void* va = pd->va; 
  pd->va = 0;
  return va;   
}


void* choose_page_to_swap_out(pde_t* pgdir){ 
  struct page_data* pd = 0;
  #ifdef LIFO
    pd = execute_LIFO();
  #endif
  #ifdef SCFIFO
    pd = execute_SCFIFO(pgdir);
  #endif
  return pd; 
}

int
next_min_swapfile_offset(void){
  struct proc* p = myproc(); 
  int current_min_offset = p->min_swapfile_offset+1 ;   
  struct page_data* pd;
  for(pd = p->pages_OUT ; pd < &p->pages_OUT[MAX_PSYC_PAGES]; pd++){
    if(pd->used){     
      if(pd->fileOffset == current_min_offset){
        current_min_offset++;
        pd = p->pages_OUT;
      }        
    }
  }
  int result = p->min_swapfile_offset;
  p->min_swapfile_offset = current_min_offset;
  return result ;  
}

void 
update_min_swapfile_offset(int unused_offset){
  struct proc* p = myproc(); 
  if(unused_offset < p->min_swapfile_offset && unused_offset !=-1 ){
    p->min_swapfile_offset = unused_offset;
    // cprintf("changed min_swapfile_offset: %d\n", unused_offset);
  }
}


void
swap_page_OUT(pde_t* pgdir){
  // choose page to move to disk
  struct page_data* pd;
  struct proc* p = myproc();
  p->paged_out_counter = p->paged_out_counter+1;
  void* va = choose_page_to_swap_out(pgdir);
  pte_t * pte = walkpgdir(pgdir,va, 0);
  int counter = 0;  
  // check if there's place on the pages_OUT
  for(pd = p->pages_OUT ; pd < &p->pages_OUT[MAX_PSYC_PAGES]; pd++){
    if(!pd->used){
      pd->used = 1;
      pd->fileOffset = next_min_swapfile_offset();     
      pd->va = va;
      break;      
    }
    counter++;
  }
  if(counter == MAX_PSYC_PAGES){     
    // did not found an empty place in OUT pages
    char* buffer = kalloc();
    memset(buffer, 0, PGSIZE);
    memmove(buffer, va, PGSIZE); 

    p->temp_page.buffer = buffer;
    p->temp_page.va = va;
  }
  else{    
    if(!(*pte & PTE_P)){
      cprintf("does not have flag PTE_P\n");
    } 
    if(!(*pte & PTE_W)){    
      cprintf("does not have flag PTE_W\n");
    }  
    writeToSwapFile(p,(char*)va,pd->fileOffset*PGSIZE,PGSIZE);  
  
  }  
  uint pa = PTE_ADDR(*pte);
  char *v = P2V(pa);  
  kfree(v);   
  if((*pte & PTE_P) || (*pte & PTE_PG)){ 
    *pte &= ~PTE_P;
    *pte|= PTE_PG;
  }
  else{
    cprintf("execute_SCFIFO: failed to update PTE_PG PTE_P\n");
  }  
  if(pgdir == p->pgdir)
    lcr3(V2P(pgdir));
}

int
get_pages_count(struct page_data* pages_meta_data){
  int counter = 0;
  struct page_data* pd; 

  for(pd = pages_meta_data ; pd < &pages_meta_data[MAX_PSYC_PAGES]; pd++){
    if(pd->used)
      counter++;
  }
  return counter;
}

int
get_write_protected_pages_count(struct proc* p, struct page_data* pages_meta_data){
  // RETURN PAGES COUNT THAT don't hava PTE_W
  int counter = 0;
  struct page_data* pd; 
  pte_t *pte;
  for(pd = pages_meta_data ; pd < &pages_meta_data[MAX_PSYC_PAGES]; pd++){
    if(pd->used){
      pte = walkpgdir(p->pgdir, (char*)pd->va, 0);      
      if(*pte & PTE_PR)
        counter++;
    }
  }
  return counter;
}


void
swap_page_IN(void* va, pde_t* pgdir){
  struct proc *p = myproc();
  void* requested_page_start = (void*) PGROUNDDOWN((uint)va);   
 
  if(get_pages_count(p->pages_IN) == MAX_PSYC_PAGES)
    swap_page_OUT(pgdir); 
  char *ka = kalloc();

  struct page_data *pd = get_page_data(p->pages_OUT,requested_page_start);

  memset(ka, 0, PGSIZE);
  
  // mapping in pte
  pte_t *pte = walkpgdir(pgdir, requested_page_start, 0);  
  pte_t flags = PTE_P | PTE_U;
  int PTE_W_idicator = 0;
  if(*pte & PTE_PR){
    flags |= PTE_PR;
  }
  if(*pte & PTE_W){
    PTE_W_idicator = 1;    
  }    
  *pte = V2P(ka) | flags | PTE_W;

  if(pd->fileOffset >=0 ){     
    readFromSwapFile(p,(char*)requested_page_start,pd->fileOffset*PGSIZE,PGSIZE);   
    if(PTE_W_idicator == 0){
      *pte &= ~PTE_W;
    }
  }  
  if(p->temp_page.buffer!=0){
    // need to backup the file 
    pd->va = p->temp_page.va;   
    writeToSwapFile(p,p->temp_page.buffer,pd->fileOffset*PGSIZE,PGSIZE);
    kfree(p->temp_page.buffer);
    p->temp_page.buffer = 0;
    p->temp_page.va = 0;
    update_min_swapfile_offset(pd->fileOffset);
    pd->fileOffset = next_min_swapfile_offset(); 
  }
  else{
    pd->used = 0;
    if(pd->fileOffset > -1){
      update_min_swapfile_offset(pd->fileOffset);
    }
  }
  long long time_counter = add_to_upages(p->pages_IN,(void*)requested_page_start, p->time_load_counter);
  if(time_counter < 0){
    cprintf("swap_page_IN failed in add_to_upages\n");   
    kfree(ka);
    panic("add_to_upages failed");
  }
  else{
    p->time_load_counter = time_counter;
  }
  // print_upages(p->pages_IN, "in swap_page_IN IN end");
  if(pgdir == p->pgdir)
    lcr3(V2P(pgdir));
}
