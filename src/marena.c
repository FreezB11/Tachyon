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

    for(int i = 0; i < CHUNKS; i++){
        uint64_t bit = 1ULL << i;

        if(!(a->free_map & bit)){
            a->free_map |= bit;
            return  (uint8_t *)a->start + (i*a->chunk_size);
        }
    }
    return NULL;
}

bool marena_free(slab *a, void *ptr){
    if(!a || !ptr) return false;
 
    /* which chunk number is this pointer? */
    ptrdiff_t offset = (uint8_t *)ptr - (uint8_t *)a->start;
    if(offset < 0) return false;
 
    size_t index = (size_t)offset / a->chunk_size;
    if(index >= CHUNKS) return false;
 
    a->free_map &= ~(1ULL << index); /* mark that chunk free again */
    return true;
}