#include"co_routine.h"
#include"co_routine_inner.h"
#include"co_epoll.h"

#include<string.h>
#include<stdlib.h>
#include<stdio.h>
#include<string>
#include<map>
#include<poll.h>
#include<sys/time.h>
#include<errno.h>
#include<assert.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/syscall.h>
#include<unistd.h>

extern "C"{
   extern void coctx_swap(coctx_t*,coctx_t*) asm("coctx_swap");
};

using namespace std;
stCoRoutine_t* GetCurrCo(stCoRoutineEnv_t* env);//获取到当前的协程
struct stCoEpoll_t;

//协程调度器，每一个线程都拥有一个
struct stCoRoutineEnv_t{
    //协程的调用栈为了支持嵌套调用而引入的
    stCoRoutine_t* pCallStack[128];
    int iCallStackSize; //当前正在运行的协程在调用栈的位置

    stCoEpoll_t* pEpoll; //协程调度器

    //共享栈的情况下需要用到的成员
    //在co_swap函数中，如果接下来要执行的协程使用的是共享栈的话
    stCoRoutine_t* pending_co;//记录要抢占对于共享栈的协程
    stCoRoutine_t* occupy_co; //以前使用共享栈的协程
};

void co_log_err(const char* fmt,...){
}

#if defined(__LIBCO_RDTSCP__)
//用来实现定时器的定时功能，对于时间的计算方式，一种是通过gettimeofday,另一种就是下面这种先通过rdtscp得到时间脉冲数，在处于CPU频率
static unsigned long long counter(void){
    register uint32_t lo,hi;
    register unsigned long long o;
    //asm 内嵌汇编代码
    //volatile 阻止编译器进行优化
    __asm__ __volatile__(
            "rdtscp" : "=a"(lo),"=d"(hi)::"%rcx"
            ); //eax寄存器的值赋值给lo,edx寄存器的值赋值给hi
    o = hi;
    o <<= 32;
    return (o | lo);
}
//得到CPU的频率
static unsigned long long getCpuKhz(){
    FILE* fp = fopen("/proc/cpuinfo","r");
    if(!fp){
        return 1;
    }
    char buf[4096] = {0};
    fread(buf,1,sizeof(buf),fp);
    fclose(fp);

    char* lp = strstr(buf,"cpu MHz");
    if(!lp){
        return 1;
    }
    lp += strlen("cpu MHz");
    while(*lp == ' ' || *lp == '\t' || *lp == ':'){
        lp++;
    }
    double mhz = atof(lp);
    unsigned long long u = (unsigned long long)(mhz * 1000);
    return u;
}
#endif

//获取时间的函数
static unsigned long long GetTickMS(){
#if defined(__LIBCO__RDTSCP__)
    static uint32_t khz = getCpuKhz();
    return counter() / khz;
#else
    //另一种求时间的方法
    struct timeval tv;
    gettimeofday(&tv,NULL);
    unsigned long long u = tv.tv_sec;
    u *= 1000; //转化成微秒
    u += tv.tv_usec / 1000;
    return u;
#endif
}

static pid_t GetPid(){
    static __thread pid_t pid = 0;
    static __thread pid_t tid = 0;
    if(!pid || !tid || pid != getpid()){
        pid = getpid();
#if defined(__APPLE__)
        tid = syscall(SYS_gettid);
        if(-1 ==(long)tid){
            tid = pid;
        }
#elif defined(__FreeBSD__)
        syscall(SYS_thr_self,&tid);
        if(tid < 0){
            tid = pid;
        }
#else
        tid = syscall(__NR_gettid);
#endif
    }
    return tid;
}

//通过模板来实现，凡是双向链表的结构都可以使用
template<class T,class TLink>
void RemoveFromLink(T* ap){
    //从TLink类型的链表中删除掉ap节点
    //这一条链表还是一条双向链表
    TLink* lst = ap->pLink;
    if(!lst){
        return ;
    }
    assert(lst->head && lst->tail);

    if(ap == lst->head){
        //如果要删除的这个节点是该链表的头节点的话
        lst->head = ap->pNext;
        if(lst->head){
            lst->head->pPrev = NULL;
        }
    }else{
        if(ap->pPrev){
            ap->pPrev->pNext = ap->pNext;
        }
    }
    //如果是尾节点的话
    if(ap == lst->tail){
        lst->tail = ap->pPrev;
        if(ap->pPrev){
            ap->pPrev->pNext = NULL;
        }
    }else{
        ap->pNext->pPrev = ap->pPrev;
    }
    ap->pPrev = ap->pNext = NULL;
    ap->pLink = NULL;
}

//尾插
template<class TNode,class TLink>
void inline AddTail(TLink* apLink,TNode* ap){
    if(ap->pLink){
        return ;
    }
    if(apLink->tail){
        apLink->tail->pNext = (TNode*)ap;
        ap->pPrev = apLink->tail;
        ap->pNext = NULL;
        apLink->tail = ap;
    }else{
        apLink->tail = ap;
        apLink->head = ap;
        ap->pPrev = NULL;
        ap->pNext = NULL;
    }
    ap->pLink = apLink;
}

//头删
template<class TNode,class TLink>
void inline PopHead(TLink* apLink){
    if(!apLink->head){
        return ;
    }
    TNode* ap = apLink->head;
    if(apLink->head == apLink->tail){
        apLink->head = apLink->tail = NULL;
    }else{
        apLink->head = ap->pNext;
    }
    ap->pPrev = ap->pNext = NULL;
    ap->pLink = NULL;
    if(apLink->head){
        apLink->head->pPrev = NULL;
    }
    return ;
}
template<class TNode,class TLink>
void inline Join(TLink* apLink,TLink* apOther){
    //apother链表添加到apLink中去
    if(!apOther->head){
        return ;
    }
    TNode* ap = apOther->head;
    while(ap){
        ap->pLink = apLink;
        ap = ap->pNext;
    }
    ap = apOther->head;
    if(apLink->tail){
        apLink->tail->pNext = (TNode*)ap;
        ap->pPrev = apLink->tail;
        apLink->tail = apOther->tail;
    }else{
        apLink->tail = apOther->tail;
        apLink->head = apOther->head;
    }
    apOther->head = apOther->tail = NULL;
}

//根据传入的大小分配一个栈,独有栈的模式下会使用到.该协程库默认使用的就是独有栈，大小是128k
stStackMem_t* co_alloc_stackmem(unsigned int stack_size){
    stStackMem_t* stack_mem = (stStackMem_t*)malloc(sizeof(struct stStackMem_t));
    stack_mem->occupy_co = NULL;
    stack_mem->stack_size = stack_size;
    stack_mem->stack_buffer = (char*)malloc(stack_size);
    stack_mem->stack_bp = stack_mem->stack_buffer + stack_size;

    return stack_mem;
}

//创建一个共享栈管理结构体
stShareStack_t* co_alloc_sharestack(int count,int stack_size){
    stShareStack_t* share_stack = (stShareStack_t*)malloc(sizeof(struct stShareStack_t));
    share_stack->alloc_idx = 0;
    share_stack->stack_size = stack_size;

    share_stack->count = count;
    stStackMem_t** stack_array = (stStackMem_t**)calloc(count,sizeof(stStackMem_t*));
    for(int i = 0;i < count;++i){
        stack_array[i] = co_alloc_stackmem(stack_size);
    }
    share_stack->stack_array = stack_array;

    return share_stack;
}
//获取一个共享栈
static stStackMem_t* co_get_stackmem(stShareStack_t* share_stack){
    if(!share_stack){
        return NULL;
    }
    int idx = share_stack->alloc_idx % share_stack->count;//重复对栈帧进行使用
    share_stack->alloc_idx++;//下一次使用的栈帧

    return share_stack->stack_array[idx];
}

struct stTimeoutItemLink_t; //超时链表
struct stTimeoutItem_t;   //链表中的元素

//协程调度器
struct stCoEpoll_t{
    int iEpollFd;  //epoll的文件描述符
    static const int _EPOLL_SIZE = 1024 * 10;  //epoll_wait最多可以返回的就绪事件个数

    struct stTimeout_t* pTimeout;//时间轮

    struct stTimeoutItemLink_t* pstTimeoutList; //临时存放定时器的超时事件
    struct stTimeoutItemLink_t* pstActiveList;  //用于存放epoll_wait返回的就绪事件和定时器的超时事件

    co_epoll_res* result; //epoll返回的结果存储在这里
};

typedef void (* onPreparePfn_t)(stTimeoutItem_t*,struct epoll_event& ev,
                                   stTimeoutItemLink_t* active);
typedef void (* onProcessPfn_t)(stTimeoutItem_t*);

//定时任务
struct stTimeoutItem_t{
    enum{
        eMaxTimeout = 40 * 1000 //40s
    };
    stTimeoutItem_t* pPrev;
    stTimeoutItem_t* pNext;

    stTimeoutItemLink_t* pLink; //属于哪一条链表

    unsigned long long ullExpireTime; //超时时间

    onPreparePfn_t pfnPrepare; //预处理函数
    onProcessPfn_t pfnProcess; //处理函数

    void* pArg;//以上两个函数的参数

    bool bTimeout;//是否超时
};

struct stTimeoutItemLink_t{
    stTimeoutItem_t* head;
    stTimeoutItem_t* tail;
};

//时间轮,精度是毫秒级的.单层级时间轮
struct stTimeout_t{
    //超时数组，长度默认为60 * 1000,数组中每一项代表一毫秒
    stTimeoutItemLink_t* pItems;
    int iItemSize; //默认是60 * 1000

    unsigned long long ullStart;//开始时间,也代表最早超时的时间
    long long llStartIdx;//时间轮的指向
};

stTimeout_t* AllocTimeout(int iSize){
    stTimeout_t* lp = (stTimeout_t*)calloc(1,sizeof(stTimeout_t));
    lp->iItemSize = iSize;
    lp->pItems = (stTimeoutItemLink_t*)calloc(1,sizeof(stTimeoutItemLink_t) * iSize);
    lp->ullStart = GetTickMS();
    lp->llStartIdx = 0;
}

void FreeTimeout(stTimeout_t* apTimeout){
    free(apTimeout->pItems);
    free(apTimeout);
}
//添加定时器
//allNow 当前时间
int AddTimeout(stTimeout_t* apTimeout,stTimeoutItem_t* apItem,unsigned long long allNow){
    if(apTimeout->ullStart == 0){
        apTimeout->ullStart = allNow;
        apTimeout->llStartIdx = 0;
    }

    //表示还未开始
    if(allNow < apTimeout->ullStart){
       co_log_err("CO_ERR: AddTimeout line %d allNow %llu apTimeout->ullStart %llu",__LINE__,allNow,apTimeout->ullStart);
       return __LINE__;
    }

    //表示要添加的定时器已经超时
    if(apItem->ullExpireTime < allNow){
        co_log_err("CO_ERR: AddTimeout line %d apItem->ullExpireTime %llu allNow %llu apTimeout->ullStart %llu",__LINE__,apItem->ullExpireTime,allNow,apTimeout->ullStart);
        return __LINE__;
    }

    unsigned long long diff = apItem->ullExpireTime - apTimeout->ullStart; //时间间隔
    if(diff >= (unsigned long long)apTimeout->iItemSize){
        diff = apTimeout->iItemSize - 1;
        co_log_err("CO_ERR: AddTimeout line %d diff %d",__LINE__,diff);
    }
    AddTail(apTimeout->pItems + ((apTimeout->llStartIdx + diff) % apTimeout->iItemSize),apItem);
    return 0;
}

//取出所有超时的定时器,在每一次的事件循环中都会被调用
//allNow 截至时间
//apReuslt 存储最终的结果
inline void TakeAllTimeout(stTimeout_t* apTimeout,unsigned long long allNow,stTimeoutItemLink_t* apResult){
    if(apTimeout->ullStart == 0){
        //还未进行初始化
        apTimeout->ullStart = allNow;
        apTimeout->llStartIdx = 0;
    }
    if(allNow < apTimeout->ullStart){
        return ;
    }
    int cnt = allNow - apTimeout->ullStart + 1;  //计算出超时的槽的个数 1ms->一个槽
    if(cnt > apTimeout->iItemSize){
        cnt = apTimeout->iItemSize;
    }
    if(cnt < 0){
        return ;
    }
    //挨个槽进行处理
    for(int i = 0;i < cnt;++i){
        int idx = (apTimeout->llStartIdx + i) % apTimeout->iItemSize;
        Join<stTimeoutItem_t,stTimeoutItemLink_t>(apResult,apTimeout->pItems + idx);
    }
    apTimeout->ullStart = allNow;//更新为当前时间
    apTimeout->llStartIdx += cnt - 1;
}
//协程回调函数的wrapper
static int CoRoutineFunc(stCoRoutine_t* co,void *){
    if(co->pfn){
        co->pfn(co->arg);
    }
    co->cEnd = 1;
    stCoRoutineEnv_t* env = co->env;
    //回溯上一层协程去执行
    co_yield_env(env);
    return 0;
}
//根据传入的协程调度器和属创建出一个协程
struct stCoRoutine_t* co_create_env(stCoRoutineEnv_t* env,const stCoRoutineAttr_t* attr,pfn_co_routine_t pfn,void* arg){
    stCoRoutineAttr_t at;
    if(attr){
        memcpy(&at,attr,sizeof(at));
    }
    if(at.stack_size <= 0){
        at.stack_size = 128 * 1024;//默认是128k
    }else if(at.stack_size > 1024 * 1024 * 8){  //最大是8M
        at.stack_size = 1024 * 1024 * 8;
    }
    if(at.stack_size & 0xFFF){//进行进位操作,向4k进行对齐
        at.stack_size &= ~0xFFF; //前12位清零
        at.stack_size += 0x1000;
    }
    stCoRoutine_t* co = (stCoRoutine_t*)malloc(sizeof(stCoRoutine_t));
    memset(co,0,(long)(sizeof(stCoRoutine_t)));

    co->env = env;
    co->pfn = pfn;
    co->arg = arg;

    stStackMem_t* stack_mem = NULL;
    if(at.share_stack){
        //如果使用的是共享栈
        stack_mem = co_get_stackmem(at.share_stack);
        at.stack_size = at.share_stack->stack_size;
    }else{
        stack_mem = co_alloc_stackmem(at.stack_size);
    }

    co->stack_mem = stack_mem;

    co->ctx.ss_sp = stack_mem->stack_buffer;
    co->ctx.ss_size = at.stack_size;

    co->cStart = 0;
    co->cEnd = 0;
    co->cIsMain = 0;
    co->cEnableSysHook = 0;//默认不开启hook
    co->cIsShareStack = at.share_stack != NULL;

    co->save_size = 0;
    co->save_buffer = NULL;

    return co;
}

int co_create(stCoRoutine_t** ppco,const stCoRoutineAttr_t* attr,pfn_co_routine_t pfn,void* arg){
    if(!co_get_curr_thread_env()){
        co_init_curr_thread_env();
    }

    stCoRoutine_t* co = co_create_env(co_get_curr_thread_env(),attr,pfn,arg);

    *ppco = co;
    return 0;
}

void co_free(stCoRoutine_t* co){
    if(!co->cIsShareStack){
        free(co->stack_mem->stack_buffer);
        free(co->stack_mem);
    }else{
       //如果是共享栈，可能还保存有上一次栈的上下文
       if(co->save_buffer){
           free(co->save_buffer);
       }
       if(co->stack_mem->occupy_co == co){
           co->stack_mem->occupy_co = NULL;
       }
    }
    free(co);
}

void co_release(stCoRoutine_t* co){
    co_free(co);
}
void co_swap(stCoRoutine_t* curr,stCoRoutine_t* pending_co);

//切换到co协程去执行
//libco里面的协程只有两个状态，分别是running和pending状态
void co_resume(stCoRoutine_t* co){
   stCoRoutineEnv_t* env = co->env;

   //找到正在正在执行的协程
   stCoRoutine_t* lpCurrRoutine = env->pCallStack[env->iCallStackSize - 1];
   if(!co->cStart){
       coctx_make(&co->ctx,(coctx_pfn_t)CoRoutineFunc,co,0);
       co->cStart = 1;
   }
   env->pCallStack[env->iCallStackSize++] = co;

   //进行上下文的切换
   co_swap(lpCurrRoutine,co);
}


void co_reset(stCoRoutine_t* co){
    if(!co->cStart || co->cIsMain){
        return ;
    }
    co->cStart = 0;
    co->cEnd = 0;

    if(co->save_buffer){
        free(co->save_buffer);
        co->save_buffer = NULL;
        co->save_size = 0;
    }
    if(co->stack_mem->occupy_co == co){
        co->stack_mem->occupy_co = NULL;
    }
    return ;
}

//将当前协程挂起，恢复到上一层的协程去执行
void co_yield_env(stCoRoutineEnv_t* env){
    //由于这里没有检查就直接-2，所以在调用co_yield_env之前必须要先co_resume

    //上一层协程
   stCoRoutine_t* last = env->pCallStack[env->iCallStackSize - 2];
   //当前协程
   stCoRoutine_t* curr = env->pCallStack[env->iCallStackSize - 1];
   
   env->iCallStackSize--;

   co_swap(curr,last);
}

void co_yield_ct(){
    co_yield_env(co_get_curr_thread_env());
}

void co_yield(stCoRoutine_t* co){
    co_yield_env(co->env);
}
//在进行切换上下文切换时，对当前协程共享栈中的内容进行保存
void save_stack_buffer(stCoRoutine_t* occupy_co){
   stStackMem_t* stack_mem = occupy_co->stack_mem;
   
   //计算出当前共享栈的内容的多少
   int len = stack_mem->stack_bp - occupy_co->stack_sp;
   if(occupy_co->save_buffer){
       free(occupy_co->save_buffer);
       occupy_co->save_buffer = NULL;
   }
   occupy_co->save_buffer = (char*)malloc(len);
   occupy_co->save_size = len;

   memcpy(occupy_co->save_buffer,occupy_co->stack_sp,len);
}

//进行协程上下文切换的函数
//当前上下文保存到curr中
//对pending_co中的上下文进行加载
void co_swap(stCoRoutine_t* curr,stCoRoutine_t* pending_co){
    stCoRoutineEnv_t* env = co_get_curr_thread_env();

    //找到当前协程运行栈的栈顶位置
    char c;
    curr->stack_sp = &c;

    if(!pending_co->cIsShareStack){
        env->pending_co = NULL;
        env->occupy_co = NULL;
    }else{
        env->pending_co = pending_co;

        //找到当前正在与pending_co共用一个栈帧的协程
        //第一次调用通常是由主协程resume到其他的子协程
        //而主协程是拥有自己的栈的
        stCoRoutine_t* occupy_co = pending_co->stack_mem->occupy_co;
        pending_co->stack_mem->occupy_co = pending_co;

        env->occupy_co = occupy_co;
        //对等待协程的上下文进行保存
        if(occupy_co && occupy_co != pending_co){
            save_stack_buffer(occupy_co);
        }
    }
    //通过自己写的汇编代码来切换协程上下文
    coctx_swap(&curr->ctx,&pending_co->ctx);
    
    //yield回来之后,对上下文进行恢复
    stCoRoutineEnv_t* curr_env = co_get_curr_thread_env();
    stCoRoutine_t* update_occupy_co = env->occupy_co;
    stCoRoutine_t* update_pending_co = env->pending_co;

    if(update_occupy_co && update_pending_co && update_occupy_co != update_pending_co){
        if(update_pending_co->save_buffer && update_pending_co->save_size != 0){
            memcpy(update_pending_co->stack_sp,update_pending_co->save_buffer,update_pending_co->save_size);
        }
    }
    return ;
}

struct stPollItem_t;

//co_poll_inner的每一次调用都对于一个该结构体
struct stPoll_t : public stTimeoutItem_t{
    struct pollfd* fds;
    nfds_t nfds; //unsigned long int

    stPollItem_t* pPollItems;

    int iAllEventDetach;    //表示该stPoll_t已经被加入到了active链表上去了

    int iEpollFd;

    int iRaiseCnt; //prepare函数的调用次数,也就是触发的IO事件的个数,这样poll在返回时，就只需要对他进行返回就行了
};

//每一个事件都对应一个该结构体
struct stPollItem_t : public stTimeoutItem_t{
    struct pollfd* pSelf;
    stPoll_t* pPoll;

    struct epoll_event stEvent; //该成员会在调用epoll_ctl时，被用作第三个参数
};

//EPOLLRDNORM 表示有普通数据可以读
//EPOLLWRNORM 表示写普通数据不会阻塞
//EPOLLHUP 通常表示本端被挂起
static uint32_t PollEvent2Epoll(short events){
    uint32_t e = 0;
    if(events & POLLIN){ e |= EPOLLIN; }
    if(events & POLLOUT) { e |= EPOLLOUT; }
    if(events & POLLHUP) { e |= EPOLLHUP; }
    if(events & POLLERR) { e |= EPOLLERR; }
    if(events & POLLRDNORM) { e |= EPOLLRDNORM; }
    if(events & POLLWRNORM) { e |= EPOLLWRNORM; }

    return e;
}

static uint32_t EpollEvent2Poll(uint32_t events){
    short e = 0;
    if(events & EPOLLIN) { e |= POLLIN; }
    if(events & EPOLLOUT) { e |= POLLOUT; }
    if(events & EPOLLHUP) { e |= POLLHUP; }
    if(events & EPOLLERR) { e |= POLLERR; }
    if(events & EPOLLRDNORM) { e |= POLLRDNORM; }
    if(events & EPOLLWRNORM) { e |= POLLWRNORM; }

    return e;
}

static __thread stCoRoutineEnv_t* gCoEnvPerThread = NULL;

void co_init_curr_thread_env(){
    gCoEnvPerThread = (stCoRoutineEnv_t*)calloc(1,sizeof(stCoRoutineEnv_t));
    stCoRoutineEnv_t* env = gCoEnvPerThread;

    env->iCallStackSize = 0;
    //对主协程进行创建，主协程我们就可以将它看作main函数
    struct stCoRoutine_t* self = co_create_env(env,NULL,NULL,NULL);
    self->cIsMain = 1;

    env->pending_co = NULL;
    env->occupy_co = NULL;

    coctx_init(&self->ctx);
    env->pCallStack[env->iCallStackSize++] = self;

    stCoEpoll_t* ev = AllocEpoll();
    SetEpoll(env,ev);
}

stCoRoutineEnv_t* co_get_curr_thread_env(){
    return gCoEnvPerThread;
}

//挂起的协程的处理函数
void OnPollProcessEvent(stTimeoutItem_t* ap){
    stCoRoutine_t* co = (stCoRoutine_t*) ap->pArg;
    co_resume(co);
}

//挂起的协程的预处理函数
void OnPollPreparePfn(stTimeoutItem_t* ap,struct epoll_event& e,stTimeoutItemLink_t* active){
    stPollItem_t* lp = (stPollItem_t*)ap;
    //将实际发生的事件设置到绑定的stPoll_t里面的那个epoll_event里面去
    lp->pSelf->revents = EpollEvent2Poll(e.events);

    stPoll_t* lPoll = lp->pPoll;
    lPoll->iRaiseCnt++;

    if(!lPoll->iAllEventDetach){
        //如果还没有被处理过的话
        lPoll->iAllEventDetach = 1;
        RemoveFromLink<stTimeoutItem_t,stTimeoutItemLink_t>(lPoll); 

        AddTail(active,lPoll);
    }
    return ;
}

//事件循环,一般在主协程被调用
void co_eventloop(stCoEpoll_t* ctx,pfn_co_eventloop_t pfn,void* arg){
    if(!ctx->result){
        ctx->result = co_epoll_res_alloc(stCoEpoll_t::_EPOLL_SIZE);
    }
    co_epoll_res* result = ctx->result;

    for(;;){
      int ret = co_epoll_wait(ctx->iEpollFd,result,stCoEpoll_t::_EPOLL_SIZE,1);

      stTimeoutItemLink_t* active = ctx->pstActiveList; //可能里面已经有了事件
      stTimeoutItemLink_t* timeout = ctx->pstTimeoutList;

      memset(timeout,0,sizeof(stTimeoutItemLink_t));
      //对就绪的事件进行处理
      for(int i = 0;i < ret;++i){
          stTimeoutItem_t* item = (stTimeoutItem_t*)result->events[i].data.ptr;
          if(item->pfnPrepare){
              //IO事件
              item->pfnPrepare(item,result->events[i],active);
          }else{
              AddTail(active,item);
          }
      }
      //超时事件
      unsigned long long now = GetTickMS();
      TakeAllTimeout(ctx->pTimeout,now,timeout);

      stTimeoutItem_t* lp = timeout->head;
      while(lp){
          lp->bTimeout = true;
          lp = lp->pNext;
      }
      Join<stTimeoutItem_t,stTimeoutItemLink_t>(active,timeout);

      lp = active->head;
      while(lp){
          PopHead<stTimeoutItem_t,stTimeoutItemLink_t>(active);
          if(lp->bTimeout && now < lp->ullExpireTime){
              //如果是超时事件,但是没到超时时间
              int ret = AddTimeout(ctx->pTimeout,lp,now);
              if(!ret){
                  lp->bTimeout = false;
                  lp = active->head;
                  continue;
              }
          }
          if(lp->pfnProcess){
              //默认调用的是OnPollProcessEvent,通过co_resume对协程进行恢复
              lp->pfnProcess(lp);
          }
          lp = active->head;
      }
      if(pfn){
          if(-1 == pfn(arg)){
              break;
          }
      }
    }
    return ;
}

void OnCoroutineEvent(stTimeoutItem_t* ap){
    stCoRoutine_t* co = (stCoRoutine_t*)ap->pArg;
    co_resume(co);
}

stCoEpoll_t* AllocEpoll(){
    stCoEpoll_t* ctx = (stCoEpoll_t*)calloc(1,sizeof(stCoEpoll_t));
    ctx->iEpollFd = co_epoll_create(stCoEpoll_t::_EPOLL_SIZE);
    ctx->pTimeout = AllocTimeout(60 * 1000); //创建时间轮

    ctx->pstActiveList = (stTimeoutItemLink_t*)calloc(1,sizeof(stTimeoutItemLink_t));
    ctx->pstTimeoutList = (stTimeoutItemLink_t*)calloc(1,sizeof(stTimeoutItemLink_t));

    return ctx;
}

void FreeEpoll(stCoEpoll_t* ctx){
    if(ctx){
        free(ctx->pstTimeoutList);
        free(ctx->pstActiveList);
        FreeTimeout(ctx->pTimeout);
        co_epoll_res_free(ctx->result);
        //TODO
        //这里好像没有对epoll_create得到的文件描述符进行释放
        close(ctx->iEpollFd);
    }
    free(ctx);
}

//获取调度器当前正在执行的协程
stCoRoutine_t* GetCurrCo(stCoRoutineEnv_t* env){
    return env->pCallStack[env->iCallStackSize - 1];
}
//回去当前线程正在执行的协程
stCoRoutine_t* GetCurrThreadCo(){
    stCoRoutineEnv_t* env = co_get_curr_thread_env();
    if(!env){ return NULL; }
    return GetCurrCo(env);
}

typedef int (* poll_pfn_t)(struct pollfd fds[],nfds_t nfds,int timeout);

// 第二个参数表示的是要监听的文件描述符
// pollfunc 表示的是原始的poll函数
int co_poll_inner(stCoEpoll_t* ctx,struct pollfd fds[],nfds_t nfds,int timeout,poll_pfn_t pollfunc){
  //负责将大部分的sys_hook注册到epoll中去  
  if(timeout > stTimeoutItem_t::eMaxTimeout){
      timeout = stTimeoutItem_t::eMaxTimeout;
  }
  int epfd = ctx->iEpollFd;
  
  stCoRoutine_t* self = co_self();

  stPoll_t& arg = *((stPoll_t*)malloc(sizeof(stPoll_t)));
  memset(&arg,0,sizeof(arg));

  arg.iEpollFd = epfd;
  arg.nfds = nfds;
  arg.fds = (pollfd*)calloc(nfds,sizeof(pollfd));

  stPollItem_t arr[2];
  if(nfds < sizeof(arr) / sizeof(arr[0]) && !self->cIsShareStack){
      //如果要注册事件不超过两个并且当前线程使用的共享栈的话
      arg.pPollItems = arr;
  }else{
      arg.pPollItems = (stPollItem_t*)malloc(nfds * sizeof(stPollItem_t));
  }
  memset(arg.pPollItems,0,nfds * sizeof(stPollItem_t));

  arg.pfnProcess = OnPollProcessEvent;   //用于超时时使用
  arg.pArg = GetCurrCo(co_get_curr_thread_env());

  //add event
  //
  for(int i = 0;i < nfds;++i){
      arg.pPollItems[i].pSelf = arg.fds + i;
      arg.pPollItems[i].pPoll = &arg;

      arg.pPollItems[i].pfnPrepare = OnPollPreparePfn;

      struct epoll_event& ev = arg.pPollItems[i].stEvent;
      if(fds[i].fd > -1){
          //表示注册的就是fd事件
          ev.data.ptr = arg.pPollItems + i;
          ev.events = PollEvent2Epoll(fds[i].events);
          //对事件进行添加
          int ret = co_epoll_ctl(epfd,EPOLL_CTL_ADD,fds[i].fd,&ev);

          if(ret < 0 && errno == EPERM && nfds == 1 && pollfunc != NULL){
              //注册失败
              if(arg.pPollItems != arr){
                  free(arg.pPollItems);
                  arg.pPollItems = NULL;
              }
              free(arg.fds);
              free(&arg);

              //资源失败完毕之后调用最原始的poll
              return pollfunc(fds,nfds,timeout);
          }
      }
  }

      //add timeout
    unsigned long long now = GetTickMS();
    arg.ullExpireTime = now + timeout;
    int ret = AddTimeout(ctx->pTimeout,&arg,now);

    if(ret != 0){
          //添加失败的话
        co_log_err("CO_ERR: AddTimeout ret %d now %lld timeout %d arg.ullExpireTime %lld",ret,now,timeout,arg.ullExpireTime);
        errno = EINVAL;

        if(arg.pPollItems != arr){
            free(arg.pPollItems);
            arg.pPollItems = NULL;
        }
        free(arg.fds);
        free(&arg);

         return - __LINE__;
    }

      //注册完事件之后,就可以yield会上一层协程去执行了
    co_yield_env(co_get_curr_thread_env());

      //切换回来之后,一般是定时器超时/等待的事件发送之后，才会co_yield回来
      //将stPoll_t从时间轮移除
    RemoveFromLink<stTimeoutItem_t,stTimeoutItemLink_t>(&arg);

      //将事件从epoll中进行注销
    for(nfds_t i = 0;i < nfds;++i){
        int fd = fds[i].fd;
        if(fd > -1){
            co_epoll_ctl(epfd,EPOLL_CTL_DEL,fd,&arg.pPollItems[i].stEvent);
        }
        fds[i].revents = arg.fds[i].revents;
    }
    int iRaiseCnt = arg.iRaiseCnt;
    if(arg.pPollItems != arr){
        free(arg.pPollItems);
        arg.pPollItems = NULL;
    }
    free(arg.fds);
    free(&arg);
    return iRaiseCnt; //检测到的注册的IO事件的个数
}

int co_poll(stCoEpoll_t* ctx,struct pollfd fds[],nfds_t nfds,int timeout_ms){
    return co_poll_inner(ctx,fds,nfds,timeout_ms,NULL);
}

void SetEpoll(stCoRoutineEnv_t* env,stCoEpoll_t* ev){
      env->pEpoll = ev;
}

stCoEpoll_t* co_get_epoll_ct(){
   if(!co_get_curr_thread_env()){
         co_init_curr_thread_env();
    }
    return co_get_curr_thread_env()->pEpoll;
}

struct stHookPThreadSpec_t{
    stCoRoutine_t* co;
    void* value;
    enum{
        size = 1024
    };
};

//通过源码可以看出，如果当前是线程的话，那么co = NULL,那么执行的就是pthread_getspecific,
//相当于就是线程局部变量.
//如果是真正的子协程的话，那么进行在协程控制块内部的局部变量成员中从查找的
void* co_getspecific(pthread_key_t key){
    stCoRoutine_t* co = GetCurrThreadCo();
    if(!co || co->cIsMain){
        return pthread_getspecific(key);
    }
    return co->aSpec[key].value;
}
int co_setspecific(pthread_key_t key,const void* value){
    stCoRoutine_t* co = GetCurrThreadCo();
    if(!co || co->cIsMain){
        return pthread_setspecific(key,value);
    }
    co->aSpec[key].value = (void*)value;
    return 0;
}

//关闭当前协程的hook功能
void co_disable_hook_sys(){
    stCoRoutine_t* co = GetCurrThreadCo();
    if(co){
        co->cEnableSysHook = 0;
    }
}


//检测是否打开hook功能
bool co_is_enable_sys_hook(){
    stCoRoutine_t* co = GetCurrThreadCo();
    return (co && co->cEnableSysHook);
}


stCoRoutine_t* co_self(){
    return GetCurrThreadCo();
}

//条件变量
struct stCoCond_t;
struct stCoCondItem_t{
    stCoCondItem_t* pPrev;
    stCoCondItem_t* pNext;
    stCoCond_t* pLink;

    stTimeoutItem_t timeout;
};
struct stCoCond_t{
    stCoCondItem_t* head;
    stCoCondItem_t* tail;
};
//唤醒阻塞在条件变量上的协程的回调函数
static void OnSignalProcessEvent(stTimeoutItem_t* ap){
    stCoRoutine_t* co = (stCoRoutine_t*)ap->pArg;
    co_resume(co);
}
stCoCondItem_t* co_cond_pop(stCoCond_t* link);

int co_cond_signal(stCoCond_t* si){
    stCoCondItem_t* sp = co_cond_pop(si);
    if(!sp){
        return 0;
    }
    RemoveFromLink<stTimeoutItem_t,stTimeoutItemLink_t>(&sp->timeout);

    //添加到avtive中去，等待在eventloop中进行处理
    AddTail(co_get_curr_thread_env()->pEpoll->pstActiveList,&sp->timeout);

    return 0;
}
int co_cond_broadcast(stCoCond_t* si){
    for(;;){
        stCoCondItem_t* sp = co_cond_pop(si);
        if(!sp){
            return 0;
        }
        RemoveFromLink<stTimeoutItem_t,stTimeoutItemLink_t>(&sp->timeout);

        AddTail(co_get_curr_thread_env()->pEpoll->pstActiveList,&sp->timeout);
    }
    return 0;
}

int co_cond_timedwait(stCoCond_t* link,int ms){
    stCoCondItem_t* psi = (stCoCondItem_t*)calloc(1,sizeof(stCoCondItem_t));
    psi->timeout.pArg = GetCurrThreadCo();
    psi->timeout.pfnProcess = OnSignalProcessEvent;

    if(ms > 0){
        unsigned long long now = GetTickMS();
        psi->timeout.ullExpireTime = now + ms;

        int ret = AddTimeout(co_get_curr_thread_env()->pEpoll->pTimeout,&psi->timeout,now);
        if(ret != 0){
            free(psi);
            return ret;
        }
    }
    AddTail(link,psi);

    co_yield_ct();//切换到其他协程去执行

    RemoveFromLink<stCoCondItem_t,stCoCond_t>(psi);

    free(psi);
    return 0;
}

stCoCond_t* co_cond_alloc(){
    return (stCoCond_t*)calloc(1,sizeof(stCoCond_t));
}

int co_cond_free(stCoCond_t* cc){
    free(cc);
    return 0;
}
stCoCondItem_t* co_cond_pop(stCoCond_t* link){
    stCoCondItem_t* p = link->head;
    if(p){
        PopHead<stCoCondItem_t,stCoCond_t>(link);
    }
    return p;
}

