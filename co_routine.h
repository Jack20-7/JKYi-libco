#ifndef _JKYI_LIBCO_COROUTINE_H_
#define _JKYI_LIBCO_COROUTINE_H_

#include<stdint.h>
#include<sys/poll.h>
#include<pthread.h>

struct stCoRoutine_t;
struct stShareStack_t;

//协程属性
struct stCoRoutineAttr_t{
    int stack_size;  //如果协程使用共享栈的话这个成员的值就不需要指定
    stShareStack_t* share_stack;

    stCoRoutineAttr_t(){
        stack_size = 128 * 1024; //默认采用的是独有栈
        share_stack = NULL;
    }
}__attribute__((packed)); //取消编译器在编译时采用的优化对齐

struct stCoEpoll_t;
typedef int (* pfn_co_eventloop_t) (void *);
typedef void* (* pfn_co_routine_t)(void*);

int co_create(stCoRoutine_t** co,const stCoRoutineAttr_t* attr,void* (*routine)(void*),void* arg);//创建协程
void co_resume(stCoRoutine_t* co); //调度该协程
void co_yield(stCoRoutine_t* co); //切换回主协程执行
void co_yield_ct();
void co_release(stCoRoutine_t* co);

stCoRoutine_t* co_self();

int co_poll(stCoEpoll_t* ctx,struct pollfd fds[],nfds_t nfds,int timeout_ms);
void co_eventloop(stCoEpoll_t* ctx,pfn_co_eventloop_t pfn,void* arg);

//specific
int co_setspecific(pthread_key_t key,const void * value);
void* co_getspecific(pthread_key_t key);

//event
stCoEpoll_t* co_get_epoll_ct();

//hook syscall (poll,read,write,recv,send,recvfrom,sendto)
void co_enable_hook_sys();
void co_disable_hook_sys();
bool co_is_enable_sys_hook();

//condition
struct stCoCond_t;
stCoCond_t* co_cond_alloc();
int co_cond_free(stCoCond_t* cc);

int co_cond_signal(stCoCond_t*);
int co_cond_broadcast(stCoCond_t*);
int co_cond_timedwait(stCoCond_t* ,int timeout_ms);

//share stack
stShareStack_t* co_alloc_sharestack(int iCount,int iStackSize);

void co_set_env_list(const char* name[],size_t cnt);
void co_log_err(const char * fmt,...);
#endif
