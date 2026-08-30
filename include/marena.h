#pragma once

#define CHUNKS 64
#include <stdint.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

typedef void ptr;

typedef struct slab_t{
    uint64_t free_map; /* we will use 1 for occupied and 0 for free */
    size_t chunk_size; /* the chunk i.e, 10KiB for the thread stack can vary later */
    void *start;       /* start of the slab where we have the ptr offset/base */
    struct slab_t *nxt;/* we alloc next slab when the current slab is fully occupied and lets say has 90% filled */
}slab;

extern slab *threads_areana;
extern slab *threads_ctx_arena;

/**
 * @note: we are asking kernel for 2MiB page here 
 * and that memory will be handled by us internally
 */
bool marena_init(slab *a, size_t chunk_size);
void *marena_alloc(slab *a);
bool marena_free(void *ptr);