// 本轮写入故障诊断：独立比较普通/锁页内存，排除LBM数值流程；保留日志后可删除。
#include <cuda_runtime.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
int main(){
 const size_t bytes=216UL*1024*1024;
 for(int mode=0;mode<2;++mode){
  void* ptr=nullptr;
  if(mode){auto e=cudaHostAlloc(&ptr,bytes,cudaHostAllocMapped);if(e!=cudaSuccess){printf("alloc_error=%s\n",cudaGetErrorString(e));return 2;}}
  else ptr=malloc(bytes);
  memset(ptr,0x5a,bytes);
  for(int repeat=0;repeat<3;++repeat){
   char path[80];snprintf(path,sizeof(path),"%s.dat",mode?"pinned":"heap");
   int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600); if(fd<0){printf("open errno=%d %s\n",errno,strerror(errno));return 3;}
   errno=0;auto n=write(fd,ptr,bytes);int we=errno;errno=0;int synced=fsync(fd);int se=errno;
   struct stat st{};fstat(fd,&st);close(fd);
   printf("kind=%s repeat=%d requested=%zu written=%zd write_errno=%d(%s) fsync=%d sync_errno=%d(%s) size=%ld\n",mode?"pinned":"heap",repeat,bytes,n,we,strerror(we),synced,se,strerror(se),(long)st.st_size);fflush(stdout);
  }
  if(mode)cudaFreeHost(ptr);else free(ptr);
 }
}
