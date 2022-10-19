#include"../co_routine.h"
#include"../co_routine_inner.h"

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include<string.h>
#include<stack>
#include<sys/socket.h>
#include<netinet/in.h>
#include<fcntl.h>
#include<arpa/inet.h>
#include<errno.h>
#include<vector>
#include<set>
#include<unistd.h>

#ifdef __FreeBSD__
#include<cstring>
#endif

using namespace std;

struct task_t{
    stCoRoutine_t* co;
    int fd;
    struct sockaddr_in addr;
};

static int SetNonBlock(int iSock){
    int iFlags;

    iFlags = fcntl(iSock,F_GETFL,0);
    iFlags |= O_NONBLOCK;
    iFlags |= O_NDELAY;
    int ret = fcntl(iSock,F_SETFL,iFlags);
    return ret;
}

static void SetAddr(const char* pszIP,const unsigned short shPort,struct sockaddr_in& addr){
    bzero(&addr,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(shPort);
    int nIP = 0;
    if(!pszIP || '\0' == *pszIP 
            || 0 == strcmp(pszIP,"0") || 0 == strcmp(pszIP,"0.0.0.0")){
        nIP = htonl(INADDR_ANY);
    }else{
        nIP = inet_addr(pszIP); 
    }
    addr.sin_addr.s_addr = nIP;
}

static int CreateTcpSocket(const unsigned short shPort = 0,const char* pszIP = "*",bool bReuse = false){
    int fd = socket(AF_INET,SOCK_STREAM,0);
    if(fd >= 0){
      if(shPort != 0){
          if(bReuse){
              int nReuseAddr = 1;
              setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&nReuseAddr,sizeof(nReuseAddr));
          }
          struct sockaddr_in addr;
          SetAddr(pszIP,shPort,addr);
          int  ret = bind(fd,(struct sockaddr*)&addr,sizeof(addr));
          if(ret != 0){
            close(fd);
            return -1;
          }
      }
    }
    return fd;
}

//该函数主要要做的事情就是
//1.根据传入的服务器IP + port，向他发起连接，并初始化task_t
//2.在while循环中，通过poll对他们的写事件进行监听，然后对没有发生写事件的socket进行重新的注册
static void* poll_routine(void* arg){
    co_enable_hook_sys();

    vector<task_t>& v = *(vector<task_t>*)arg;
    for(size_t i = 0;i < v.size();++i){
        int fd = CreateTcpSocket();
        SetNonBlock(fd);
        v[i].fd = fd;
        //
        int ret = connect(fd,(struct sockaddr*)&v[i].addr,sizeof(v[i].addr));
        printf("co %p connect i %ld ret %d errno (%s)\n",co_self(),i,ret,strerror(errno));
    }
    struct pollfd* pf = (struct pollfd*)calloc(1,sizeof(struct pollfd) * v.size());
    for(size_t i = 0;i < v.size();++i){
        pf[i].fd = v[i].fd;
        pf[i].events = (POLLOUT | POLLERR | POLLHUP);
    }
    set<int> setRaiseFds;
    size_t iWaitCnt = v.size();
    for(;;){
        int ret = poll(pf,iWaitCnt,1000);
        printf("co %p poll wait %ld ret %d\n",co_self,iWaitCnt,ret);
        for(int i = 0;i < ret;++i){
            printf("co %p fire fd %d revents 0x%x POLLOUT 0x%X POLLERR 0x%X POLLHUP 0x%X\n",
                      co_self(),
                      pf[i].fd,
                      pf[i].revents,
                      POLLOUT,
                      POLLERR,
                      POLLHUP);
            setRaiseFds.insert(pf[i].fd);
        }
        if(setRaiseFds.size() == v.size()){
            break;
        }
        if(ret <= 0){
            break;
        }
        iWaitCnt = 0;
        for(size_t i = 0;i < v.size();++i){
            if(setRaiseFds.find(v[i].fd) == setRaiseFds.end()){
                pf[iWaitCnt].fd = v[i].fd;
                pf[iWaitCnt].events = (POLLOUT | POLLERR | POLLHUP);
                ++iWaitCnt;
            }
        }
    }
    for(size_t i = 0;i < v.size();++i){
        close(v[i].fd);
        v[i].fd = -1;
    }
    printf("co %p task cnt %ld fire %ld\n",co_self(),v.size(),setRaiseFds.size());
    return 0;
}

int main(int argc,char** argv){
    
    vector<task_t> v;
    for(int i = 1;i < argc;i += 2){
        task_t task = {0};
        SetAddr(argv[i],atoi(argv[i + 1]),task.addr);
        v.push_back(task);
    }

    printf("------------------------main----------------------\n");
    vector<task_t> v2 = v;

    //本次调用时由于没有对stCoRoutineEnv_t进行初始话，所以就是普通的调用
    poll_routine(&v2);
    printf("-----------------------routine--------------------\n");

    for(int i = 0;i < 10;++i){
        stCoRoutine_t* co = NULL;
        vector<task_t>* v2 = new vector<task_t>();
        *v2 = v;
        co_create(&co,NULL,poll_routine,v2);
        printf("routine i %d\n",i);
        co_resume(co);
    }
    co_eventloop(co_get_epoll_ct(),0,0);
    return 0;
}
