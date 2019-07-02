#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();  

  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;
  #ifndef NONE
    struct page_data *pd_old, *pd_new;
    long long load_counter_old;
    // You may assume ELF file size is smaller than 13 pages
    // (which as exec works meanmaximum 15 pages post exec).
    struct page_data pages_IN_old[MAX_PSYC_PAGES];
    struct page_data pages_IN_new[MAX_PSYC_PAGES];
    // // init arrays
    for(pd_old = pages_IN_old, pd_new = pages_IN_new; pd_old < &pages_IN_old[MAX_PSYC_PAGES]; pd_old++, pd_new++){
      pd_old->used=0;
      pd_new->used=0;
    }  
    // backup for old pgdir
    copy_meta_data(curproc->pages_IN, pages_IN_old);
    load_counter_old = curproc->time_load_counter;
    // set new meta data to new pgdir
    copy_meta_data(pages_IN_new, curproc->pages_IN);
    curproc->time_load_counter = 0;  
  #endif

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;

  #ifndef NONE
    // init swapfile
    if(curproc->pid>2){
      if(removeSwapFile(curproc)!=0){
        cprintf("exec: failed to create swapfile\n");
        goto bad;
      }
      if(createSwapFile(curproc)!=0){
        cprintf("exec: failed to create swapfile\n");
        goto bad;
      }
    }
    // freevm needs meta data for old pgdir
    copy_meta_data(curproc->pages_IN, pages_IN_new);
    copy_meta_data(pages_IN_old, curproc->pages_IN);  
  #endif

  switchuvm(curproc);   

  freevm(oldpgdir);
  #ifndef NONE
    // restore meta data for new pgdir
    copy_meta_data(pages_IN_new, curproc->pages_IN);
  #endif
  return 0;

 bad:
  #ifndef NONE
    // if something went bad so restore old image
    copy_meta_data(pages_IN_old, curproc->pages_IN);
    curproc->time_load_counter = load_counter_old;
  #endif
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  
  return -1;
}
