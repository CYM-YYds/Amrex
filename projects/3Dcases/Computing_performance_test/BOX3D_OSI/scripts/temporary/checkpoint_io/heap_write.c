/* 本次I/O配对诊断专用：仅把Cell_D文件的write/writev数据复制到普通malloc内存。
 * 不用于生产；保留日志与源码以复现，所有其他文件和系统调用沿用原路径。 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>

static int target(int fd) {
    char link[64], path[4096];
    snprintf(link,sizeof(link),"/proc/self/fd/%d",fd);
    ssize_t n=readlink(link,path,sizeof(path)-1);
    if(n<0)return 0;
    path[n]=0;
    return strstr(path,"/Cell_D_")!=NULL;
}
static void record(const char *kind,size_t bytes,ssize_t result,int error) {
    const char *path=getenv("IO_HEAP_LOG");
    if(!path)return;
    int fd=open(path,O_WRONLY|O_CREAT|O_APPEND,0600);
    if(fd<0)return;
    char line[200];int n=snprintf(line,sizeof(line),"heap_copy kind=%s bytes=%zu result=%zd errno=%d\n",kind,bytes,result,error);
    syscall(SYS_write,fd,line,(size_t)n);close(fd);
}
ssize_t write(int fd,const void *data,size_t bytes) {
    static ssize_t (*real_write)(int,const void*,size_t);
    if(!real_write)real_write=dlsym(RTLD_NEXT,"write");
    if(!target(fd))return real_write(fd,data,bytes);
    void *copy=malloc(bytes?bytes:1);
    if(!copy){errno=ENOMEM;return -1;}
    memcpy(copy,data,bytes);ssize_t n=real_write(fd,copy,bytes);int error=errno;
    free(copy);record("write",bytes,n,n<0?error:0);errno=error;return n;
}
ssize_t writev(int fd,const struct iovec *iov,int count) {
    static ssize_t (*real_writev)(int,const struct iovec*,int);
    if(!real_writev)real_writev=dlsym(RTLD_NEXT,"writev");
    if(!target(fd)||count<=0)return real_writev(fd,iov,count);
    size_t total=0;
    for(int i=0;i<count;++i){if(iov[i].iov_len>SIZE_MAX-total){errno=EINVAL;return -1;}total+=iov[i].iov_len;}
    void *copy=malloc(total?total:1);struct iovec *vec=malloc((size_t)count*sizeof(*vec));
    if(!copy||!vec){free(copy);free(vec);errno=ENOMEM;return -1;}
    size_t offset=0;
    for(int i=0;i<count;++i){vec[i].iov_base=(char*)copy+offset;vec[i].iov_len=iov[i].iov_len;memcpy(vec[i].iov_base,iov[i].iov_base,iov[i].iov_len);offset+=iov[i].iov_len;}
    ssize_t n=real_writev(fd,vec,count);int error=errno;
    free(vec);free(copy);record("writev",total,n,n<0?error:0);errno=error;return n;
}
