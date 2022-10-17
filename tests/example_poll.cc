#include"../co_routine.h"

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include<stack>
#include<sys/socket.h>
#include<netient/in.h>
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


int main(int argc,char** argv){
    
    vector<task_t> v;
    for(int i = 1;i < argc;i += 2){
        task_t task = {0};
        SetAddr(argv[1],atoi(argv[i + 1]),task.addr);
        v.push_back(task);
    }
    printf("------------------------main----------------------\n");
    vector<task_t> v2 = v;
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
