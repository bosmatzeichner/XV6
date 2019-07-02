//
// File descriptors
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

int
check_if_inude_unique(int inum, int *unique_inodes){
  // cprintf("check_if_inude_unique: inum to find: %d\n",inum);
  int *current_inum;
  for(current_inum = unique_inodes; current_inum < (unique_inodes + NFILE); current_inum++){
    if(*current_inum == -1){
      // not found in unique_inodes
      // cprintf("not found in unique_inodes\n");
      *current_inum = inum;
      return 1;     
    }
    if(*current_inum == inum){
      // found
      // cprintf("found\n");
      return 0;
    }
  }
  return 0;
}


int
get_filestat_file_string(char* dst){
  struct file *f;
  int unique_inodes[NFILE];
  int off = 0;
  int free_fds = 0;
  int unique_inode_fds = 0;
  int writeable_fds = 0;  
  int readable_fds = 0; 
  int total_number_of_refs = 0;
  int *current_inum;
  // init unique_inodes array
  for(current_inum = unique_inodes; current_inum < (unique_inodes + NFILE); current_inum++){
    *current_inum = -1;
  }
  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      free_fds+=1;
    }
    else{
      if(check_if_inude_unique(f->ip->inum, unique_inodes)){
        unique_inode_fds+=1;
      }
      if(f->readable){
        readable_fds+=1;
      }
      if(f->writable){
        writeable_fds+=1;
      }
      total_number_of_refs+=f->ref;
    }
  }

  char free_fds_str[] = "Free fds: ";
  off+=add_string_number(dst,off,free_fds_str,free_fds,add_space);
  char unique_inode_fds_str[] = "Unique inode fds: ";
  off+=add_string_number(dst,off,unique_inode_fds_str,unique_inode_fds,add_space);
  char writeable_fds_str[] = "Writeable fds: ";  
  off+=add_string_number(dst,off,writeable_fds_str,writeable_fds,add_space);
  char readable_fds_str[] = "Readable fds: "; 
  off+=add_string_number(dst,off,readable_fds_str,readable_fds,add_space);
  char refs_per_fds_str[] = "Refs per fds: "; 
  int used_fds = 100 - free_fds;
  // cprintf("used_fds: %d\n",used_fds);
  // cprintf("total_number_of_refs: %d\n",total_number_of_refs);

  float refs_per_fds = (float)total_number_of_refs/(float)used_fds;
  off+=add_string_number(dst,off,refs_per_fds_str,refs_per_fds,add_space);
  release(&ftable.lock);
  return off;
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE)
    pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
int
filestat(struct file *f, struct stat *st)
{
  if(f->type == FD_INODE){
    ilock(f->ip);
    stati(f->ip, st);
    iunlock(f->ip);
    return 0;
  }
  return -1;
}

// Read from file f.
int
fileread(struct file *f, char *addr, int n)
{
  int r;

  if(f->readable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return piperead(f->pipe, addr, n);
  if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
    return r;
  }
  panic("fileread");
}

//PAGEBREAK!
// Write to file f.
int
filewrite(struct file *f, char *addr, int n)
{
  int r;

  if(f->writable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return pipewrite(f->pipe, addr, n);
  if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    return i == n ? n : -1;
  }
  panic("filewrite");
}

