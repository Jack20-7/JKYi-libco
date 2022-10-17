.globl coctx_swap
#if !defined( __APPLE__ ) && !defined( __FreeBSD__ )
.type  coctx_swap, @function
#endif

//下面就是协程上下文切换的流程.处于性能的原因,libco中的没有使用ucontext，而是自己写的汇编来进行协程的上下文切换
coctx_swap:

#if defined(__i386__)
   //如果是32位的系统的话
   
   leal 4(%esp),%eax   //将第一个参数的地址放入到eax寄存器中去
   movl 4(%esp),%esp   //让esp指向第一个参数的地址，也就是要切换下的协程上下文的regs[0]
   lead 32(%esp),%esp  //esp-> &regs[7] + sizezeof(void*)

   //下面通过压栈的方式，将当前CPU上下文中涉及到的寄存器的值保存到regs[7] - regs[0]里面去
   pushl %eax  //第一个参数的地址放入到regs[7]

   pushl %ebp //regs[6]
   pushl %esi //regs[5]
   pushl %edi //regs[4]
   pushl %edx //regs[3]
   pushl %ecx //regs[2]
   pushl %ebx //regs[1]

   pushl -4(%eax)  //将coctx_swap的返回地址放入到regs[0]

   movl 4(%eax),%esp  //令esp指向第二个参数的地址，也就是最低地址处

   //下面就通过出栈的方式对CPU寄存器的值进行恢复

   popl %eax   //coctx函数的返回地址存储到eax寄存器
   popl %ebx
   popl %ecx
   popl %edx
   popl %edi
   popl %esi
   popl %ebp
   popl %esp

   pushl %eax //重新将返回地址放入到栈中

   xorl %eax,%eax

   ret
#elif defined(__x86_64__)
   //如果是64位的系统

   leaq 8(%rsp),%rax   
   
   //在64位的CPU中，参数是存储在寄存器中的，这里rdi寄存器中存储的就是第一个参数的地址,rsi寄存器存储的是第二个参数的地址
   leaq 112(%rdi),%rsp   

   //填充regs
   pushq %rax  //regs[13]第一个参数的地址
   pushq %rbx
   pushq %rcx
   pushq %rdx

   pushq -8(%rax) //function func addr -> regs[9]

   pushq %rsi
   pushq %rdi
   pushq %rbp
   pushq %r8
   pushq %r9
   pushq %r12
   pushq %r13
   pushq %r14 
   pushq %r15  //rip ->regs[0]

   movq %rsi,%rsp

   //下面就是通过出栈的方式对CPU寄存器的值进行加载
   popq %r15
   popq %r14
   popq %r13
   popq %r12
   popq %r9
   popq %r8
   popq %rbp
   popq %rdi
   popq %rsi
   popq %rax //function ret addr 
   popq %rdx
   popq %rcx
   popq %rbx

   popq %rsp

   pushq %rax //重新将返回地址入栈

   xorl %eax,%eax //寄存器清零，相当于就是return 0

   ret

#endif


