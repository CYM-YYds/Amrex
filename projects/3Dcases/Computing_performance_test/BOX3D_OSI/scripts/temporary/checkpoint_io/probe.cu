// 本轮写入故障诊断：独立比较普通/锁页内存，排除LBM数值流程；保留日志后可删除。
#include <cuda_runtime.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/uio.h>
int main(int argc,char** argv){
 const bool vectored=argc>1 && strcmp(argv[1],"writev")==0;
 const size_t bytes=(argc>2?strtoull(argv[2],nullptr,10):216UL)*1024*1024;
 char header[81];memset(header,0x48,sizeof(header));
 for(int mode=0;mode<2;++mode){
  void* ptr=nullptr;
  if(mode){auto e=cudaHostAlloc(&ptr,bytes,cudaHostAllocMapped);if(e!=cudaSuccess){printf("alloc_error=%s\n",cudaGetErrorString(e));return 2;}}
  else ptr=malloc(bytes);
  if(!ptr){printf("allocation_failed\n");return 2;}
  memset(ptr,0x5a,bytes);
  for(int repeat=0;repeat<(argc>3?atoi(argv[3]):3);++repeat){
   char path[80];snprintf(path,sizeof(path),"%s.dat",mode?"pinned":"heap");
   int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600); if(fd<0){printf("open errno=%d %s\n",errno,strerror(errno));return 3;}
   struct iovec iov[2]={{header,sizeof(header)},{ptr,bytes}};
   errno=0;auto n=vectored?writev(fd,iov,2):write(fd,ptr,bytes);int we=errno;errno=0;int synced=fsync(fd);int se=errno;
   struct stat st{};fstat(fd,&st);close(fd);
   printf("kind=%s repeat=%d requested=%zu written=%zd write_errno=%d(%s) fsync=%d sync_errno=%d(%s) size=%ld\n",mode?"pinned":"heap",repeat,bytes+(vectored?sizeof(header):0),n,we,strerror(we),synced,se,strerror(se),(long)st.st_size);fflush(stdout);
   // 独立回读全部字节，验证写入长度、头部和数据内容，不能只看write返回值。
   bool verified=n==static_cast<ssize_t>(bytes+(vectored?sizeof(header):0)) && synced==0;
   fd=open(path,O_RDONLY);
   if(fd<0) verified=false;
   else {
    unsigned char buffer[1024*1024];size_t offset=0;
    const size_t expected=bytes+(vectored?sizeof(header):0);
    while(offset<expected){
     const size_t remaining=expected-offset;
     const size_t chunk=remaining<sizeof(buffer)?remaining:sizeof(buffer);
     ssize_t got=read(fd,buffer,chunk);
     if(got<=0){verified=false;break;}
     for(ssize_t j=0;j<got;++j){
      const unsigned char wanted=(vectored && offset+static_cast<size_t>(j)<sizeof(header))?0x48:0x5a;
      if(buffer[j]!=wanted) verified=false;
     }
     offset+=static_cast<size_t>(got);
    }
    unsigned char extra;if(read(fd,&extra,1)!=0)verified=false;
    close(fd);
   }
   printf("readback_verified=%d\n",verified?1:0);fflush(stdout);
   if(!verified)return 4;
  }
  if(mode)cudaFreeHost(ptr);else free(ptr);
 }
}
