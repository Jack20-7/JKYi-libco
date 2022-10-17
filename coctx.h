#ifndef _JKYI_CO_CTX_H_
#define _JKYI_CO_CTX_H_

#include<stdlib.h>

typedef void * (*coctx_pfn_t) (void * s,void *s2);

struct coctx_param_t{
    const void * s1;
    const void * s2;
};

//协程的上下文
struct coctx_t{
    //切换时需要保存和加载的cpu寄存器的值
#if defined(__i386__)
    void * regs[8];
#else
    void * regs[14];
#endif

    size_t ss_size; //栈的大小
    char * ss_sp;
};

int coctx_init(coctx_t* ctx);
int coctx_make(coctx_t* ctx,coctx_pfn_t pfn,const void * s,const void * s1);

#endif
