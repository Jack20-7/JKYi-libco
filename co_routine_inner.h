#ifndef _JKYI_LIBCO_COROUTINE_INNER_H_

#include"co_routine.h"
#include"coctx.h"

struct stCoRoutineEnv_t;  //协程管理器

//协程环境变量
struct stCoSpec_t{
    void * value;
};

//共享栈中的内容,我将它看作栈帧
struct stStackMem_t{
    stCoRoutine_t* occupy_co; //当前正在使用该栈帧的协程
    int stack_size;
    char * stack_bp;  //栈的底部
    char * stack_buffer; //栈的顶部
};

//共享栈
struct stShareStack_t{
    unsigned int alloc_idx; 
    int stack_size; //每一个小的栈帧的大小
    int count; //栈帧的数目
    stStackMem_t** stack_array;
};

//协程
struct stCoRoutine_t{
    stCoRoutineEnv_t* env;  //该协程归那一个调度器管

    pfn_co_routine_t pfn;
    void * arg;
    coctx_t ctx; //协程的上下文

    //下面是一些状态标志位
    char cStart;  //是否开始运行
    char cEnd;    //是否已经结束
    char cIsMain; //是否是主协程
    char cEnableSysHook; //是否被hook
    char cIsShareStack;  //是否使用共享栈

    void* pvEnv; //用于保存程序系统环境变量的指针

    stStackMem_t* stack_mem;  //协程的运行栈

    //下面是使用共享栈的时候，会用到的成员
    char* stack_sp;  //应该表示的是运行时栈帧的的栈顶位置，在进行切换时会用到
    unsigned int save_size; 
    char* save_buffer;  //协程切换时，它的上下文会被保存到该成员指向的那一块空间中去

    //用来实现协程局部变量
    stCoSpec_t aSpec[1024];
};

void co_init_curr_thread_env(); //对当前所在线程的协程调度器进行初始化
stCoRoutineEnv_t*   co_get_curr_thread_env(); //返回当前线程的协程调度器

void co_free(stCoRoutine_t* co);
void co_yield_env(stCoRoutineEnv_t* env);

struct stTimeout_t;
struct stTimeoutItem_t;
stTimeout_t* AllocTimeout(int size);
void FreeTimeout(stTimeout_t* apTimeout);
void AddTimeout(stTimeout_t* apTimeout,stTimeoutItem_t* apItem,uint64_t allNow);

struct stCoEpoll_t;
struct stCoEpoll_t* AllocEpoll();
void FreeEpoll(stCoEpoll_t* ctx);

stCoRoutine_t* GetCurrThreadCo(); 
void SetEpoll(stCoRoutineEnv_t* env,stCoEpoll_t* ev);

typedef void (*pfnCoRoutineFunc_t)();

#endif

#define _JKYI_LIBCO_COROUTINE_INNER_H_

