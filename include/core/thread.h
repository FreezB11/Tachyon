#pragma once
#include "../arch/x86_64/context.h"
#include <stddef.h>
#include <stdint.h>   /* missing — needed for uint64_t */

#define STACK_SIZE 1024 * 10   /* 10Kib */

typedef enum{
    READY, // ready to run
    RUNNING, // already running
    BLOCKED, // blocked so can be skipped till its given its rights back lol
    WAITING, // waiting for a other process you can skip me 
    DEAD // i am dead just clean my memory
}State;

typedef
struct thread_t{
    context_t ctx;
    State state;
    void* stack;
    size_t stack_size;
    uint64_t tid;
    void       (*fn)(void*);   /* stored for reference/debugging */
    void*        arg;
}thread_t;

void      thread_ctx_init(context_t* ctx, void* stack, size_t stack_size, void (*fn)(void*), void* arg);
thread_t* t_create(void (*fn)(void*), void* arg);
void      t_destroy(thread_t* t);