#include"../co_routine.h"
#include"../co_routine_inner.h"

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include<pthread.h>
#include<unistd.h>

int loop(void* ){
    printf("function start\n");
    return 0;
}
static void* routine_func(void* arg){
    stCoEpoll_t* ev = co_get_epoll_ct();
    co_eventloop(ev,loop,0);
    return 0;
}
int main(int argc,char** argv){
    int cnt = 10;

    pthread_t tid[ cnt ];
    for(int i = 0;i < cnt;++i){
        pthread_create(&tid[i],NULL,routine_func,0);
    }
    while(1){
        sleep(1);
    }
    return 0;
}
