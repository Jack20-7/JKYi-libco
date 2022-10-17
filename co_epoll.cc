#include"co_epoll.h"

#include<stdlib.h>
#include<stdio.h>
#include<errno.h>
#include<string.h>

#if !defined(__APPLE__) && !defined(__FreeBSD__)

int co_epoll_wait(int epfd,struct co_epoll_res* events,int maxevents,int timeout){
    return epoll_wait(epfd,events->events,maxevents,timeout);
}
int co_epoll_ctl(int epfd,int op,int fd,struct epoll_event* event){
    return epoll_ctl(epfd,op,fd,event);
}
int co_epoll_create(int size){
    return epoll_create(size);
}
struct co_epoll_res* co_epoll_res_alloc(int size){
    struct co_epoll_res* ptr = 
               (struct co_epoll_res*) malloc(sizeof(struct co_epoll_res));
    ptr->size = size;
    ptr->events = (struct epoll_event*)malloc(size * sizeof(struct epoll_event));

    return ptr;
}
void co_epoll_res_free(struct co_epoll_res* ptr){
    if(!ptr){
        return ;
    }
    if(ptr->events){
        free(ptr->events);
    }
    free(ptr);
}
#else

//暂时不考虑苹果和FreeBSD
#endif

