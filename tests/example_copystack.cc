#include"../coctx.h"
#include"../co_routine.h"
#include"../co_routine_inner.h"

#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/time.h>
#include<errno.h>
#include<string.h>

//对共享栈进行测试

void* RoutineFunc(void* args){
    co_enable_hook_sys();

    int* routineid = (int*)args;
    while(true){
        char sBuff[128];
        sprintf(sBuff,"from routineid %d stack addr %p\n",*routineid,sBuff);
        printf("%s",sBuff);
        //
        poll(NULL,0,1000); //添加一个stPoll_t到时间轮,然后yield回主协程
    }
    return NULL;
}
int main(){
    stShareStack_t* share_stack = co_alloc_sharestack(1,1024 * 128);

    stCoRoutineAttr_t attr;
    attr.share_stack = share_stack;
    attr.stack_size = 0;

    stCoRoutine_t* co[2];
    int routineid[2];
    for(int i = 0;i < 2;++i){
        routineid[i] = i;

        co_create(&co[i],&attr,RoutineFunc,routineid + i);
        co_resume(co[i]);
    }
    co_eventloop(co_get_epoll_ct(),NULL,NULL);
    return 0;
}

//这里我存在一个疑问，如果是32位的系统的话，for循环中当第二次调用co_resume时，第一个子协程共享栈中的内容还未保存，由于只有一个共享栈，那么第二个子协程在创建时分配的栈也是同一个共享栈
//那么在co_resume中当判断第二个协程中的cStart = 0时，会涉及到对第二个协程调用coctx_make,而这个函数中会涉及到对那个共享栈进行操作，但是第一个协程共享栈中的内容还未保存
//所以按照我的理解，会造成第一个协程栈空间内容被破环。64位系统由于寄存器比较多，函数的参数是存储在寄存器中的，所以不会破环栈空间
