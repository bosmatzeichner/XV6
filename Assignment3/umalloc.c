#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"
#include "mmu.h"

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.

typedef long Align;
int pages_count=0;

union header {
  struct {
    union header *ptr;
    uint size;
  } s;
  Align x;
};

typedef union header Header;

static Header base;
static Header *freep;

void
free(void *ap)
{
  // if user sends protected page to free what to do?
  Header *bp, *p;

  bp = (Header*)ap - 1;
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;
  if(bp + bp->s.size == p->s.ptr){
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr;
  if(p + p->s.size == bp){
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  freep = p;
}

static Header*
morecore(uint nu)
{
  char *p;
  Header *hp;

  if(nu < 4096)
    nu = 4096;
  p = sbrk(nu * sizeof(Header));
  if(p == (char*)-1)
    return 0;
  hp = (Header*)p;
  hp->s.size = nu;
  free((void*)(hp + 1));
  return freep;
}

void*
malloc(uint nbytes)
{
  Header *p, *prevp;
  uint nunits;

  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
  if((prevp = freep) == 0){
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    if(p->s.size >= nunits){
      if(p->s.size == nunits)
        prevp->s.ptr = p->s.ptr;
      else {
        p->s.size -= nunits;
        p += p->s.size;
        p->s.size = nunits;
      }
      freep = prevp;
      return (void*)(p + 1);
    }
    if(p == freep)
      if((p = morecore(nunits)) == 0)
        return 0;
  }
}

// (1) it will always allocate exactly 1 page, 
// (2)  the page will be page-aligned,  
// (3) if there is any free memory in the previously 
// allocated page it will skip it, and the new memory
//  allocation (using malloc or pmalloc) will be made from a new page
void*
pmalloc(){

  Header *p, *prevp;
  uint nunits;
  uint end_of_aligned_page_ptr;
  int success = 0;
  int found_aligned_page = 0;
  // nunits=512
  nunits = PGSIZE / sizeof(Header);
  if((prevp = freep) == 0){
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    if(p->s.size >= nunits){     
      uint end_of_block_ptr = (uint) p + p->s.size*sizeof(Header);  
      int remain_from_PGSIZE =(int)end_of_block_ptr % PGSIZE;
      int remain_from_header = remain_from_PGSIZE % sizeof(Header);
      end_of_aligned_page_ptr = end_of_block_ptr - remain_from_PGSIZE;

      if(remain_from_header != 0 ){  
        int complete_to_header = sizeof(Header)-remain_from_header;
        end_of_aligned_page_ptr -= complete_to_header;

        // need to find alinged position which is start of header
        while(end_of_aligned_page_ptr > (uint)p){
          if(end_of_aligned_page_ptr % PGSIZE == 0){
            break;            
          }
          end_of_aligned_page_ptr -= sizeof(Header);
        }
        if(end_of_aligned_page_ptr-PGSIZE>=(uint)p){
          found_aligned_page = 1;
        }
      }
      else{
        // found end_of_block_ptr which his remain is divided by sizeof(Header);
        if(end_of_aligned_page_ptr-PGSIZE >= (uint)p){
          found_aligned_page = 1;
        }       
      }
      if(found_aligned_page){   
        int remain = (end_of_block_ptr - end_of_aligned_page_ptr)/sizeof(Header);
        if ((end_of_block_ptr - end_of_aligned_page_ptr) % sizeof(Header) != 0){
          printf(1,"pmalloc: error in remain not divided by sizeof(Header)\n");
        }
        if(p->s.size == nunits && remain == 0){
          success = 1;
          prevp->s.ptr = p->s.ptr;    
        }         
        else {       
          int temp_p_size = p->s.size - (nunits+ remain); 
          if(temp_p_size==0){
            success = 1;
            prevp->s.ptr = p->s.ptr;  
          }     
          if(temp_p_size>0){
            p->s.size = temp_p_size;
            p += p->s.size;
            p->s.size = nunits;
            success = 1;
          }
          if(success && remain>0){
            Header* end_of_aligned_page_header = (Header*) end_of_aligned_page_ptr;
            end_of_aligned_page_header->s.size = remain;
            free(end_of_aligned_page_header+1);      
          }          
        }
        // change page flag in pgdir to protected
        if (success == 1 && add_flag_to_pte(PTE_PR, p) < 0)
          return 0;  
        freep = prevp;
        pages_count++;
        return (void*)p;      
      }
    }
    if(p == freep){
      if((p = morecore(nunits)) == 0)
        return 0;
    }     
  }
}
// Similarly to free(), this function will attempt to release a
// protected page that pointed at the argument. 
// This call must only be invoked on protected pages,
//  return –1 on failure, 1 on success.
int
pfree(void* ap){
  if((uint)ap & 0xFFF){
    // printf(1,"pfree failed because pa is not start of page: %x\n",ap);
    return -1;
  }
  if(check_flag_on_pte(PTE_PR, ap)<0){
    // printf(1,"pfree failed because pa is not initialize with pmalloc\n");
    return -1;
  }
  if((uint)ap % PGSIZE != 0){  
    // printf(1,"pfree failed because pa is not aligned\n");
    return -1;
  }
  if(check_flag_on_pte(PTE_PR, ap) == 0){
    // check which flags need to remove
    remove_flag_from_pte(PTE_PR, ap);
    add_flag_to_pte(PTE_W, ap);
    Header *p = (Header *) ap;
    p->s.size= 512;
    free(p+1);
    return 1;
  }
  else{
    return -1;
  }
}

// This function takes a pointer. It will verify that
// the address of the pointer has been allocated using
// pmalloc and that it points to the start of the page.
// If the above condition holds, it will protect the page, 
// and return 1, (any attempts to write to the page will 
// result in a failure). If the above condition does not hold,
// it returns –1
int
protect_page(void* ap){
  // verify that the address of the pointer has been allocated using
  // pmalloc and that it points to the start of the page
  
  if((uint)ap & 0xFFF){
    // printf(1,"protect_page failed because pa is not start of page: %x\n",ap);
    return -1;
  }
  if(check_flag_on_pte(PTE_PR, ap)<0){
    // printf(1,"protect_page failed because pa is not initialize with pmalloc\n");
    return -1;
  }
  if((uint)ap % PGSIZE != 0){  
    // printf(1,"protect_page failed because pa is not aligned\n");
    return -1;
  }
  if(remove_flag_from_pte(PTE_W, ap) == 0)     
    return 1;  
  return -1;
}