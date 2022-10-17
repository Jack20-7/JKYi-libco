#include"coctx.h"
#include<string.h>
#include<stdio.h>

#define ESP 0
#define EIP 1
#define EAX 2
#define ECX 3

#define RSP 0
#define RIP 1
#define RBX 2
#define RDI 3
#define RSI 4

#define RBP 5
#define R12 6
#define R13 7
#define R14 8
#define R15 9
#define RDX 10
#define RCX 11
#define R8  12
#define R9  13

//----- --------
// 32 bit
// | regs[0]: ret | function ret addr
// | regs[1]: ebx |
// | regs[2]: ecx |
// | regs[3]: edx |
// | regs[4]: edi |
// | regs[5]: esi |
// | regs[6]: ebp |
// | regs[7]: eax |  = esp

enum{
 kEIP = 0,
 kESP = 7,
};

//-------------
// 64 bit
//low | regs[0]: r15 |
//    | regs[1]: r14 |
//    | regs[2]: r13 |
//    | regs[3]: r12 |
//    | regs[4]: r9  |
//    | regs[5]: r8  | 
//    | regs[6]: rbp |
//    | regs[7]: rdi |
//    | regs[8]: rsi |
//    | regs[9]: ret |  //ret func addr
//    | regs[10]: rdx |
//    | regs[11]: rcx | 
//    | regs[12]: rbx |
//hig | regs[13]: rsp |

enum{
    kRDI = 7,
    kRSI = 8,
    kRETAddr = 9,
    kRSP = 13
};

extern "C"{
   //asm可以将参数转化为汇编指令，c语言环境下直接使用汇编指针执行
   extern void coctx_swap(coctx_t*,coctx_t*) asm("coctx_swap"); 
};

#if defined(__i386__)
int coctx_init(coctx_t* ctx){
    memset(&ctx,0,sizeof(coctx_t));
    return 0;
}

int coctx_make(coctx* ctx,coctx_pfn_t pfn,const void * s,const void * s1){
   //对应的就是makecontext函数
   //make root for coctx_param_t
   char * sp = ctx->ss_sp + ctx->ss_size - sizeof(coctx_param_t);
   sp = (char*)((unsigned long)sp & -16L);

   coctx_param_t* param = (coctx_param_t*)sp;
   param->s1 = s;
   param->s2 = s1;

   memset(ctx->regs,0,sizeof(ctx->regs));
   ctx->regs[kEIP] = pfn;
   ctx->regs[kESP] = (char*)(sp) - sizeof(void*);  //给返回地址预留出空间

   return 0;
}

#elif defined(__x86_64__)
int coctx_init(coctx_t* ctx){
    memset(ctx,0,sizeof(*ctx));
    return 0;
}

int coctx_make(coctx_t * ctx,coctx_pfn_t pfn,const void * s,const void * s1){
    //可以看出，64位的系统上参数可以直接存储早CPU寄存器中
    char * sp = ctx->ss_sp + ctx->ss_size;
    sp = (char*)((unsigned long)sp & -16LL);

    memset(ctx->regs,0,sizeof(ctx->regs));
    ctx->regs[kRETAddr] = (char*)pfn;
    ctx->regs[kRSP] = sp - 8;
    ctx->regs[kRDI] = (char*)s;
    ctx->regs[kRSI] = (char*)s1;

    return 0;
}

#endif
