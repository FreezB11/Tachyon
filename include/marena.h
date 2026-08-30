#pragma once

#define THREADS 4 // here i say that we hold 4 threads
#define FACTOR 1 // this is how much each thread will take for now that is 1KiB
#define CHUNK ((THREADS)*(FACTOR)*1024) // for now its 4KiB

#include <stdint.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdbool.h>

typedef void ptr;

typedef struct slab_t{
    uint8_t *stack;
    struct slab_t *nxt;
}slab;

extern ptr* threads_areana;
extern ptr* threads_ctx_arena;

/**
 * @note: we are asking kernel for 2MiB page here 
 * and that memory will be handled by us internally
 */
bool marena_init();
slab* marena_alloc();