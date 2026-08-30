#include "../../include/core/thread.h"
#include "../../include/core/sched.h"
#include "../../include/marena.h"
#include "../../include/arch/x86_64/context.h"

#include <stdlib.h>
#include <stdint.h>

/* 0 = malloc-backed stacks, 1 = marena-backed stacks */
#ifndef USE_MARENA
#define USE_MARENA 0
#endif

// defined in context.S
extern void thread_entry(void);

void thread_ctx_init(context_t* ctx, void* stack, size_t stack_size, void (*fn)(void*), void* arg) {
    uint64_t* stack_top = (uint64_t*)((uint8_t*)stack + stack_size);

    // 16-byte align per SysV ABI
    stack_top = (uint64_t*)((uintptr_t)stack_top & ~0x0FULL);

    // push fake return address — thread_entry never returns so doesn't matter
    stack_top--;
    *stack_top = 0;
    ctx->rip    = (uint64_t)thread_entry;
    ctx->rsp    = (uint64_t)stack_top;
    ctx->rbp    = 0;
    ctx->rflags = 0x202;
    ctx->rbx    = 0;
    ctx->r8     = 0;
    ctx->r9     = 0;
    ctx->r10    = 0;
    ctx->r11    = 0;
    ctx->r12    = (uint64_t)arg;   // ← arg
    ctx->r13    = (uint64_t)fn;    // ← fn
    ctx->r14    = 0;
    ctx->r15    = 0;
}

thread_t* t_create(void (*fn)(void*), void* arg) {
    thread_t* t = malloc(sizeof(thread_t));
    if (!t) return NULL;

#if USE_MARENA
    void* stack = marena_alloc(threads_ctx_arena);
#else
    void* stack = malloc(STACK_SIZE);
#endif
    if (!stack) { free(t); return NULL; }

    t->fn         = fn;
    t->arg        = arg;
    t->stack      = stack;
    t->stack_size = STACK_SIZE;
    t->state      = READY;
    t->tid        = g_sched.next_tid++;

    thread_ctx_init(&t->ctx, stack, STACK_SIZE, fn, arg);
    return t;
}

void t_destroy(thread_t* t) {
    if (!t) return;

#if USE_MARENA
    marena_free(threads_ctx_arena, t->stack);
#else
    free(t->stack);
#endif

    free(t);
}