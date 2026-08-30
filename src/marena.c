#include "../include/marena.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

bool marena_init(slab *a, size_t chunk_size){
    if(!a || chunk_size == 0) return false;
   
   size_t region_size = chunk_size *  CHUNKS;
   void *mem = mmap(
       NULL,
       region_size,
       PROT_READ | PROT_WRITE,
       MAP_ANONYMOUS | MAP_PRIVATE,
       -1,
       0
   );

   if(mem == MAP_FAILED) return false;
   
   a->free_map = 0;
   a->chunk_size = chunk_size;
   a->start = mem;
   a->nxt = NULL;

   return true;
}

void *marena_alloc(slab *a){
    if(!a) return NULL;

    slab *cur  = a;
    slab *tail = a;

    /* walk the chain looking for a slab with a free chunk */
    while(cur){
        for(int i = 0; i < CHUNKS; i++){
            uint64_t bit = 1ULL << i;

            if(!(cur->free_map & bit)){
                cur->free_map |= bit;
                return (uint8_t *)cur->start + (i * cur->chunk_size);
            }
        }
        tail = cur;
        cur  = cur->nxt;
    }

    /* every slab in the chain is full — grow: alloc + init a new one */
    slab *fresh = malloc(sizeof(slab));
    if(!fresh) return NULL;

    if(!marena_init(fresh, a->chunk_size)){
        free(fresh);
        return NULL;
    }

    tail->nxt = fresh;

    /* fresh slab is empty, bit 0 is guaranteed free */
    fresh->free_map |= 1ULL;
    return fresh->start;
}

bool marena_free(slab *a, void *ptr){
    if(!a || !ptr) return false;

    slab *cur = a;

    while(cur){
        ptrdiff_t offset   = (uint8_t *)ptr - (uint8_t *)cur->start;
        ptrdiff_t region_sz = (ptrdiff_t)(cur->chunk_size * CHUNKS);

        /* does ptr actually fall inside THIS slab's region? */
        if(offset >= 0 && offset < region_sz){
            size_t index = (size_t)offset / cur->chunk_size;
            cur->free_map &= ~(1ULL << index); /* mark that chunk free again */
            return true;
        }

        cur = cur->nxt;
    }

    return false; /* ptr didn't belong to any slab in the chain */
}