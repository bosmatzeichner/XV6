#include "types.h"
#include "stat.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"


#define INUM_START 202
#define FILESTAT_INUM  203
#define INODEINFO_INUM 204
#define INODE_INFO_FILES_INUM_START 205

#define INODE_INFO_FILES_INUM_LIMIT 405
#define PROC_INUM_START 406

int proc_dir_inum = -1;

int 
procfsisdir(struct inode *ip) {
  int is_inodeinfo_dir = ((ip->inum == INODEINFO_INUM) && (ip->type == T_DEV))? 1:0;
  int j = ip->inum-PROC_INUM_START;

  int is_process_dir = (((j>=0) && (j%3 == 0)) && (ip->type == T_DEV))? 1:0;
  if(is_inodeinfo_dir){
    // cprintf("procfsisdir: is_inodeinfo_dir\n");
    return 1;
  } 
  if(is_process_dir){
    // cprintf("procfsisdir: is_process_dir j: %d\n",j);
    return 1;
  }
  if((ip->inum <NINODE) && (ip->type == T_DEV)){
    proc_dir_inum = ip->inum;
    // cprintf("procfsisdir: is_proc_dir inum:%d\n",ip->inum);
    return 1;
  }
  // cprintf("procfsisdir: not a directory\n");
  return 0;
}

void 
procfsiread(struct inode* dp, struct inode *ip) {
  // This function can update all ip fields Note that if ip->valid is not set need to read from disk
  // init values
  ip->major = PROCFS;
  ip->type = T_DEV;
  ip->valid = 1; 
}

void
add_dirent(char *dst, int off, int inum,char* name){
  struct dirent new_dirent;
  new_dirent.inum = inum;
  memset(new_dirent.name, 0,strlen(new_dirent.name));
  memmove(new_dirent.name,name,strlen(name));
  // to check if copied on the end of dst string
  dst= dst+off;
  memmove(dst,(char*)&new_dirent,sizeof(new_dirent));  
}

int
add_string_number(char *dst, int off,char* string,int number, int to_add_space){  
  int writen_off = 0;
  dst= dst+off;
  char buf[256];
  memset(buf,0,256);   
  memmove(dst,string,strlen(string));  
  writen_off+=strlen(string);
  dst= dst+strlen(string);
  if(number>=0){
    itoa(number,buf);
    memmove(dst,buf,strlen(buf));
    dst= dst+strlen(buf);
    writen_off+=strlen(buf);
  }  
  if(to_add_space){
    memmove(dst,"\n",strlen("\n"));
    writen_off+=1;
  }
  return writen_off;
}

int
get_proc_directory_string(char* dst, int current_dir_inum){
  int pid;
  int i; 
  int off = 0;
  int inum = 0;
  struct dirent de;
  // for .
  char current_dir_name[] = ".";
  inum = current_dir_inum;
  add_dirent(dst, off,inum,current_dir_name);
  off+=sizeof(de); 

  // for ..
  char parent_dir_name[] = "..";
  inum = 1;
  add_dirent(dst, off,inum,parent_dir_name);
  off+=sizeof(de);
  
  // for ideinfo file
  char ideinfo_name[] = "ideinfo";
  inum = INUM_START;
  add_dirent(dst, off,inum,ideinfo_name);
  off+=sizeof(de);

  // for filestat file
  char filestat_name[] = "filestat";
  inum = INUM_START+1;
  add_dirent(dst, off,inum,filestat_name);
  off+=sizeof(struct dirent);

  // for filestat file
  char inodeinfo_name[] = "inodeinfo";
  inum = INUM_START+2;
  add_dirent(dst, off,inum,inodeinfo_name);
  off+=sizeof(struct dirent);
  // cprintf("before calc process off %d\n", off);
  int num_process =0;


  // for process directorys 
  for(i=0;i<NPROC;i++){
    struct proc* p = get_pid_from_ptable_index(i);
    if(p != 0){
      pid = p->pid;
      // the index i is occupied by process with pid
      char proc_pid_str[256];
      memset(proc_pid_str,0,256);
      itoa(pid,proc_pid_str);
      inum = PROC_INUM_START + i*3;
      add_dirent(dst,off,inum,proc_pid_str);
      off+=sizeof(struct dirent);
      num_process++;
    }
  }
  off-=sizeof(struct dirent);
  // cprintf("finished get_proc_directory_string num_process = %d off %d\n", off, num_process);
  return off;
}

int
get_process_directory_string(char* dst, int current_dir_inum){
  int off = 0;  
  int inum = 0;

  // for .
  char current_dir_name[] = ".";
  inum = current_dir_inum;
  add_dirent(dst, off,inum,current_dir_name);
  off+=sizeof(struct dirent); 

  // for ..
  char parent_dir_name[] = "..";
  inum = proc_dir_inum;
  add_dirent(dst, off,inum,parent_dir_name);
  off+=sizeof(struct dirent);

  // for ideinfo file
  char process_name[] = "name";
  inum = current_dir_inum+1;
  add_dirent(dst, off,inum,process_name);
  off+=sizeof(struct dirent);

  // for filestat file
  char status_name[] = "status";
  inum = current_dir_inum+2;
  add_dirent(dst, off,inum,status_name);
  // off+=sizeof(struct dirent);
  return off;
}


int
get_inodeinfo_directory_string(char* dst, int current_dir_inum){
  int off = 0;  
  int index_in_inodes_table = 0;
  struct inode *inode_table_ip;
  int inum = 0;

  // for .
  char current_dir_name[] = ".";
  inum = current_dir_inum;
  add_dirent(dst, off,inum,current_dir_name);
  off+=sizeof(struct dirent); 

  // for ..
  char parent_dir_name[] = "..";
  inum = proc_dir_inum;
  add_dirent(dst, off,inum,parent_dir_name);
  off+=sizeof(struct dirent);

  int in_inodeinfo_directory = 1;

  for(index_in_inodes_table=0;index_in_inodes_table<NINODE;index_in_inodes_table++){
    if((inode_table_ip = get_ip_from_icache_index(index_in_inodes_table,in_inodeinfo_directory))!= 0){     
      char inode_index_str[256];
      memset(inode_index_str,0,256);
      itoa(index_in_inodes_table,inode_index_str);
      inum = INODE_INFO_FILES_INUM_START + index_in_inodes_table;     
      // cprintf("get_inodeinfo_directory_string loop: index_in_inodes_table: %d,  inum: %d real inum: %d\n",index_in_inodes_table, inum, inode_table_ip->inum); 
      add_dirent(dst,off,inum,inode_index_str);
      off+=sizeof(struct dirent);
    }
  }
  off-=sizeof(struct dirent);
  return off;
}

int
get_directory_string(char *dst, int inum) {  
  if (inum == proc_dir_inum){  
      // main directory 
      return get_proc_directory_string(dst, inum);  
  }
  if(inum == INODEINFO_INUM){
    // inodeinfo directory
      return get_inodeinfo_directory_string(dst, inum);
  }   
  // for process directory
  return get_process_directory_string(dst, inum);  
}

int
handle_directory(struct inode *ip, char *dst, int off, int n) {  

  int string_off = 0;  
  // process+first files + currentdir_parentdir =  NPROC+3+2
  // for NINODE inodes and parentdir and current dir = NINODE+2
  int num_of_dirents_in_inodeinfo_directory = NINODE+2;
  // for name and status and parentdir and current dir = 2+2
  int num_of_dirents_in_directory_string = num_of_dirents_in_inodeinfo_directory;
  int size_for_directory_string = num_of_dirents_in_directory_string*sizeof(struct dirent);
  char directory_string[size_for_directory_string]; 
  memset(directory_string, 0,strlen(directory_string));

  string_off = get_directory_string(directory_string, ip->inum);

  if(off>string_off){
    return 0;
  }

  char* full_dir = directory_string+off;     
  memmove(dst,full_dir,n);      
  return n;  
}

int
get_blocks_used(struct inode *ip){
  int blocks_used = (ip->size / BSIZE) + ((ip->size % BSIZE) != 0 ? 1 : 0);  
  return blocks_used;
}

int
get_inodeinfo_file_string(char* dst, int inum){
  int off = 0;
  struct inode *ip;
  int index_in_inodes_table = inum - INODE_INFO_FILES_INUM_START;  
  // cprintf("requested index: %d inum: %d\n",index_in_inodes_table, inum);
  int in_inodeinfo_directory = 0;

  if((ip = get_ip_from_icache_index(index_in_inodes_table,in_inodeinfo_directory))!= 0){ 

    char device_str [] = "Device: ";
    off+=add_string_number(dst,off,device_str,ip->dev,add_space);
    char inode_number_str [] = "Inode number: ";
    off+=add_string_number(dst,off,inode_number_str,ip->inum,add_space);
    char is_valid_str [] = "is valid: ";
    off+=add_string_number(dst,off,is_valid_str,ip->valid,add_space);
    char type_str [] = "type: ";
    off+=add_string_number(dst,off,type_str,-1,no_space);
    
    char dir_str [] = "DIR";
    char file_str [] = "FILE";
    char dev_str [] = "DEV";
    switch(ip->type){    
      case T_DIR:
        off+=add_string_number(dst,off,dir_str,-1,add_space);
        break;

      case T_FILE:
        off+=add_string_number(dst,off,file_str,-1,add_space);
        break;

      case T_DEV:
        off+=add_string_number(dst,off,dev_str,-1,add_space);
        break;
    }
    char major_minor_str [] = "major minor: ";
    off+=add_string_number(dst,off,major_minor_str,-1,no_space);
    char left[] = "("; 
    off+=add_string_number(dst,off,left,ip->major,no_space);
    char comma[] = ","; 
    off+=add_string_number(dst,off,comma,ip->minor,no_space);
    char right[] = ")"; 
    off+=add_string_number(dst,off,right,-1,add_space);
    char hard_links_str [] = "hard links: ";
    off+=add_string_number(dst,off,hard_links_str,ip->nlink,add_space);
    char blocks_used_str [] = "blocks used: ";
    int blocks_used = (ip->type !=T_DEV)? get_blocks_used(ip):0;   
    off+=add_string_number(dst,off,blocks_used_str,blocks_used,add_space);
    return off;    
  }
  // cprintf("not found ip num on icache\n"); 
  return off;
}

int
get_status_file_string(char* dst, int inum){
  int off = 0;
  int i = (inum -PROC_INUM_START)/ 3;
  struct proc* p = get_pid_from_ptable_index(i);
  char process_state_str [] = "Process State: ";
  off+=add_string_number(dst,off,process_state_str,p->state,add_space);
  char process_memory_usage_str [] = "Memory Usage: ";
  off+=add_string_number(dst,off,process_memory_usage_str,p->sz,add_space);
  return off;
}

int
get_name_file_string(char* dst, int inum){
  int off = 0;
  int i = (inum -PROC_INUM_START)/ 3;
  struct proc* p = get_pid_from_ptable_index(i);
  char process_name_str [] = "Process Name: ";
  off+=add_string_number(dst,off,process_name_str, -1,no_space);  
  off+=add_string_number(dst,off,p->name,-1,add_space);  
  return off;
}

int
get_file_string(char *dst, int inum) {  
  // cprintf("get_file_string inum: %d INODE_INFO_FILES_INUM_START: %d,INODE_INFO_FILES_INUM_LIMIT: %d\n",inum,INODE_INFO_FILES_INUM_START,INODE_INFO_FILES_INUM_LIMIT);
  if(inum == INUM_START){  
      // ideinfo file 
     return get_ideinfo_file_string(dst); 
  }

  if(inum == FILESTAT_INUM){     
    // filestat file
      // cprintf("get_file_string filestat\n");
      return get_filestat_file_string(dst);
  }
  if(inum >= INODE_INFO_FILES_INUM_START && inum< INODE_INFO_FILES_INUM_LIMIT){
    // inodeinfo files
      // cprintf("get_file_string inodeinfo files\n");
      return get_inodeinfo_file_string(dst, inum);
  }  
  // for process files
  if(inum%3 == 2){
    return get_name_file_string(dst, inum);
  }
  return get_status_file_string(dst, inum);

}

int
read_from_file(struct inode *ip, char *dst, int off, int n){  
  int string_off = 0;   
  char file_string[PGSIZE]; 
  memset(file_string, 0,strlen(file_string));
  string_off = get_file_string(file_string, ip->inum);
  char* full_dir = file_string+off;     
  memmove(dst,full_dir,n);     
  if(off>string_off){
    return 0;
  }  
  n = (string_off-off<n)?string_off-off:n;   
  return n;  
}

int
procfsread(struct inode *ip, char *dst, int off, int n) {
  // check if the parent folder is an a directory
  if(procfsisdir(ip)){  
    return handle_directory(ip, dst,off,n);
  }
  // read from file 
  return read_from_file(ip,dst,off,n);
}

int
procfswrite(struct inode *ip, char *buf, int n)
{
  return -1;
}

void
procfsinit(void)
{
  devsw[PROCFS].isdir = procfsisdir;
  devsw[PROCFS].iread = procfsiread;
  devsw[PROCFS].write = procfswrite;
  devsw[PROCFS].read = procfsread;
}



