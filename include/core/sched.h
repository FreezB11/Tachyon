#pragma once
#include "thread.h"

//active threads list here
//finsished/zombie threads
#define MAX_THREADS 256

typedef struct sched_t {
    context_t  main_ctx;/* main()'s saved context          */
    thread_t*  queue[MAX_THREADS];/* circular run queue              */
    thread_t*  sleep[MAX_THREADS];/* this will be the sleeping queue */
    thread_t*  zombie[MAX_THREADS];/* dead threads that will be on the burner to free mem*/
    int        head;/* next thread to run              */
    int        tail;/* where to enqueue next           */
    int        count;/* live threads currently in queue */
    thread_t*  current;/* thread running right now        */
    uint64_t   next_tid;/* increments for each new thread  */
} sched_t;

extern sched_t g_sched;              /* single global scheduler instance */

void      sched_init();
void      sched_push(thread_t* t);
thread_t* sched_pop();