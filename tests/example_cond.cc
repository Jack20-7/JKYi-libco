#include"../co_routine.h"

#include<unistd.h>
#include<stdio.h>
#include<stdlib.h>
#include<queue>

using namespace std;

//对条件变量进行测试

struct stTask_t{
    int id;
};
struct stEnv_t{
    stCoCond_t* cond; //条件变量
    queue<stTask_t*> task_queue;
};
void* Producer(void* arg){
   co_enable_hook_sys();
   stEnv_t* env = (stEnv_t*)arg;

   int id = 0;
   while(true){
       stTask_t* task = (stTask_t*)calloc(1,sizeof(stTask_t));
       task->id = id++;
       env->task_queue.push(task);
       printf("%s:%d produce task %d\n",__func__,__LINE__,task->id);
       co_cond_signal(env->cond); //这里会将消费者添加的stCoCondItem_t拿出，然后将它添加到stCoEpoll_t的active链表上去
       poll(NULL,0,1000);  //yield back main fiber  添加一个stPoll_t到时间轮
   }
}

void* Consumer(void* arg){
   co_enable_hook_sys();
   stEnv_t* env = (stEnv_t*)arg;
   while(true){
       if(env->task_queue.empty()){
           co_cond_timedwait(env->cond,-1);   //会添加一个stCoCondItem_t 到 stCoCond_t链表中去,然后co_yield会主协程
           continue;
       }
       stTask_t* task = env->task_queue.front();
       env->task_queue.pop();
       printf("%s:%d consume task %d\n",__func__,__LINE__,task->id);
       free(task);
   }
}

int main(){
    stEnv_t* env = new stEnv_t;
    env->cond = co_cond_alloc();

    stCoRoutine_t* consumer_routine; //消费者协程
    co_create(&consumer_routine,NULL,Consumer,env);
    co_resume(consumer_routine);

    //
    stCoRoutine_t* producer_routine; //生产者协程
    co_create(&producer_routine,NULL,Producer,env);
    co_resume(producer_routine);

    co_eventloop(co_get_epoll_ct(),NULL,NULL); //主协程主要负责其他协程的调度,主要要处理的事件包括IO事件、定时事件、调度走的协程
    return 0;
}
