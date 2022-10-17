#ifndef _JKYI_LIBCO_CO_EPOLL_H_
#define _JKYI_LIBCO_CO_EPOLL_H_

#include<stdint.h>
#include<stdlib.h>
#include<assert.h>
#include<string.h>
#include<sys/types.h>
#include<time.h>


//只考虑在linuxOS上运行



#if !defined(__APPLE__) && !defined(__FreeBSD__)
#include<sys/epoll.h>

//epoll_wait返回的结果
struct co_epoll_res{
    int size;
    struct epoll_event* events;
    //struct kevent* eventlist;       //为了适配unix上的kqueue
};

int co_epoll_wait(int epfd,struct co_epoll_res* events,int maxevents,int timeout);
int co_epoll_ctl(int epfd,int op,int fd,struct epoll_event*);
int co_epoll_create(int size);
struct co_epoll_res* co_epoll_res_alloc(int n);
void co_epoll_res_free(struct co_epoll_res* );

#else

#include<sys/event.h>

enum EPOLL_EVENTS{
    EPOLLIN = 0x001,
    EPOLLPRI = 0x002,
    EPOLLOUT = 0x004,

    EPOLLERR = 0x008,
    EPOLLHUP = 0x010,

    EPOLLRDNORM = 0x40,
    EPOLLWRNORM = 0x004,
};
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

typedef union epoll_data{
    void* ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;
}epoll_data_t;

struct epoll_event{
    uint32_t events;
    epoll_data_t data;
};
struct co_epoll_res{
    int size;
    struct epoll_event* events;
    struct kevent* eventlist;
};

int co_epoll_wait(int epfd,struct co_epoll_res* events,int maxevents,int timeout);
int co_epoll_ctl(int epfd,int op,int fd,struct epoll_event*);
int co_epoll_create(int size);
struct co_epoll_res* co_epoll_res_alloc(int n);
void co_epoll_res_free(struct co_epoll_res*);

#endif
#endif

