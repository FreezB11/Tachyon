#include "../include/tachyon.h"
#include "../include/core/sched.h"
#include "../include/core/thread.h"
#include "../include/arch/x86_64/context.h"
#include "../include/marena.h"

slab *threads_ctx_arena;

/* tachyon_init*/
void _tachyon_init() {
    threads_ctx_arena = malloc(sizeof(slab));
    marena_init(threads_ctx_arena, STACK_SIZE);
    sched_init();
}

/* tachyon_thread*/
thread_t* tachyon_thread(void (*func)(void*), void* arg) {
    thread_t* t = t_create(func, arg);
    if (!t) return NULL;

    /* do NOT push to queue — we are switching to it right now */
    thread_t* caller = g_sched.current;
    t->state        = RUNNING;
    g_sched.current = t;

    if (caller) {
        ctx_switch(&caller->ctx, &t->ctx);
    } else {
        ctx_switch(&g_sched.main_ctx, &t->ctx);
    }

    return t;
}

/* tachyon_spool*/
/* Spawn n threads all running the same func.*/
void tachyon_spool(int n, void (*func)(void*), void* arg) {
    for (int i = 0; i < n; i++)
        tachyon_thread(func, arg);
}

/* tachyon_yield*/
/* Current thread gives up CPU, next thread in queue runs.*/
void tachyon_yield() {
    thread_t* current = g_sched.current;
    thread_t* next    = sched_pop();

    if (!next) return;   /* nobody else to run, just continue */

    if (current) {
        /* yielding from a thread — re-enqueue it, switch to next */
        current->state  = READY;
        sched_push(current);
        next->state     = RUNNING;
        g_sched.current = next;
        ctx_switch(&current->ctx, &next->ctx);
    } else {
        /* yielding from main */
        next->state     = RUNNING;
        g_sched.current = next;
        ctx_switch(&g_sched.main_ctx, &next->ctx);
    }
}

/* tachyon_thread_exit */
/* Called by thread_entry in asm when fn() returns.*/
void tachyon_thread_exit() {
    thread_t* current = g_sched.current;
    current->state    = DEAD;

    thread_t* next = sched_pop();

    if (next) {
        next->state     = RUNNING;
        g_sched.current = next;
        ctx_switch(&current->ctx, &next->ctx);
    } else {
        /* nothing left, back to main */
        g_sched.current = NULL;
        ctx_switch(&current->ctx, &g_sched.main_ctx);
    }
    /* unreachable */
}

/* tachyon_join */
/* Block until thread t is DEAD, then free it.*/
void tachyon_join(thread_t* t) {
    while (t->state != DEAD)
        tachyon_yield();
    t_destroy(t);
}