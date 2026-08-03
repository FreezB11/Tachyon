/*context.S
byte = 1 byte
word = 2 byte
double word = 4 byte
quad word = 8 byte
mov a, b
copies a to b
*/

.globl ctx_switch
.type ctx_switch, @function
ctx_switch:
    /* rdi -> old context */
    /* rsi -> new context */
    /* the `call` instruction that invoked this function pushed
    the return address (rip) onto the stack. we read it from (%rsp)*/
    movq (%rsp), %rax
    movq %rax, 0(%rdi) /* rip */

    leaq 8(%rsp), %rax
    movq %rax, 8(%rdi) /* rsp: save pre-call rsp (rsp+8), not rsp which points AT the return addr */

    /*movq %rsp, 8(%rdi) rsp */
    movq %rbp, 16(%rdi)/* rbp */
    /* flags also i will like to save */
    /* 24(%rdi) */
    pushfq
    popq 24(%rdi)       /* rflags*/
    movq %rbx, 32(%rdi) /* rbx */

    movq %r12, 72(%rdi) 
    movq %r13, 80(%rdi)
    movq %r14, 88(%rdi)
    movq %r15, 96(%rdi)

    /* load the context to the regis */
    movq 32(%rsi), %rbx
    movq 72(%rsi), %r12 /* arg */
    movq 80(%rsi), %r13 /* fn  */
    movq 88(%rsi), %r14
    movq 96(%rsi), %r15

    movq 16(%rsi), %rbp

    /* restore flags */
    pushq 24(%rsi)
    popfq

    movq 8(%rsi), %rsp

    pushq 0(%rsi)
    ret

.globl ctx_save
.type ctx_save, @function
ctx_save:
    /* rdi -> context to save into */
    movq (%rsp),  %rax
    movq %rax,    0(%rdi)    /* rip */
    leaq 8(%rsp), %rax
    movq %rax,    8(%rdi)    /* rsp */
    movq %rbp,    16(%rdi)   /* rbp */
    pushfq
    popq          24(%rdi)   /* rflags */
    movq %rbx,    32(%rdi)   /* rbx */
    movq %r12,    72(%rdi)
    movq %r13,    80(%rdi)
    movq %r14,    88(%rdi)
    movq %r15,    96(%rdi)
    ret                      /* just saves and returns, no switch */

.globl thread_entry
.type thread_entry, @function
thread_entry:
    movq %r12, %rdi      /* arg → rdi (1st arg per SysV ABI) */
    callq *%r13          /* call fn(arg)                      */
    callq tachyon_thread_exit

.section .note.GNU-stack,"",@progbits