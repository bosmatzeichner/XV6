#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

// Write a user program called “lsnd” that will pull the following information for each inode in use:

int parse(char * dst, char *buf, char c1, char c2, int off){
    memset(dst,0,10);
//   for (i=0; i<10; i++){
//   	dst[i] = 0; //clear buffer
//   }
	int s = index_of(buf,c1,off);
	int e = index_of(buf,c2,s+1);
  memmove(dst, buf+s+1, e-s-1);
  return e;
}

void print_file(char * buf, char * path){
    char parse_buf[10];
    char type[6];
    char *temp_buf;

    // printf(1,"buffer: %s\n",buf);

// <#device>
    int off = parse(parse_buf, buf, ' ', '\n', 0);
    int device = atoi(parse_buf);
// <#inode>
    off = parse(parse_buf, buf, ':', '\n', off+1);
    temp_buf = parse_buf;
    temp_buf+=1;
    int inum = atoi(temp_buf);
// <is valid>
    off = parse(parse_buf, buf, ':', '\n', off+1);
    temp_buf = parse_buf;
    temp_buf+=1;   
    int is_valid = atoi(temp_buf);
// <type>
    parse(type, buf, ' ', '\n', off+1);
// <(major,minor)>
    parse(parse_buf, buf, '(', ',', off+1);
    int major = atoi(parse_buf);
    off = parse(parse_buf, buf, ',', ')',off+1);
     int minor = atoi(parse_buf);
// <hard links>
    off = parse(parse_buf, buf, ':', '\n', off+1);
    temp_buf = parse_buf;
    temp_buf+=1;    
    int nlinks = atoi(temp_buf);
// <blocks used>
    off = parse(parse_buf, buf, ':', '\n', off+1);
    temp_buf = parse_buf;
    temp_buf+=1; 
    int blocks_used = atoi(temp_buf); 

  printf(1, "%d %d %d %s (%d,%d) %d %d\n",device, inum, is_valid, type, major, minor,nlinks,blocks_used);
//   <#device> <#inode> <is valid> <type> <(major,minor)> <hard links> <blocks used>
}


void open_file(char *path){
	char buf[512];
	int fd;
  if((fd = open(path, 0)) < 0){
    printf(2, "open_file: cannot open %s\n", path);
    return;
  }
  int n;
  if((n = read(fd, buf, sizeof(buf))) > 0)
    print_file(buf, path);
  if(n < 0)
    printf(2, "open_file: read error\n");
  close(fd);
}


void
lsnd (char *path){
    char buf[512], *p;   
    int fd;
    struct dirent de;

    if((fd = open(path, 0)) < 0){
        printf(2, "lsnd: cannot open %s\n", path);
        return;
    }
    
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
        printf(1, "lsnd: path too long\n");
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
        if(de.inum == 0)
        continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0; 
        // printf(1,"lsnd: try to open path %s\n",buf);
        if(strcmp(de.name,".")==0 || strcmp(de.name,"..")==0)
            continue;
        open_file(buf);       
    }
    close(fd);    
    }

    int main(void) 
    {
        lsnd("proc/inodeinfo");
        exit();
}
