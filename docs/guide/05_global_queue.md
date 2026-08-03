# Phase 5 — Global Injector Queue & Load Balancing

## Why this phase exists
Work stealing (phase 4) balances load *between* procs once routines
already exist somewhere. But two situations still aren't handled:

1. A routine spawned from code that isn't currently running as any
   routine at all (e.g. called from a signal handler, from a callback in
   some other library, or before any proc's loop has started) has no
   "calling proc" to enqueue onto locally.
2. Even with stealing, if load arrives in a bursty, uneven way, procs can
   still end up briefly starved between steal attempts.

A **global injector queue** (Go calls this the "global run queue") is a
single shared, lock-protected (or lock-free) queue that any proc can push
into or pop from, used as a fallback/overflow — not the primary path.

## What to build

### `src/core/global_queue.h` / `.c`
```c
typedef struct global_queue {
    pthread_mutex_t lock;
    routine_t*      head;   /* simple intrusive linked list is fine here --
                                this queue is meant to be low-traffic, so a
                                lock + linked list is simpler and correct,
                                unlike the hot-path local runq which needed
                                Chase-Lev for performance. */
    routine_t*      tail;
    int              count;
} global_queue_t;

void       global_queue_init(global_queue_t* q);
void       global_queue_push(global_queue_t* q, routine_t* r);
routine_t* global_queue_pop(global_queue_t* q);
```
Note `routine_t` needs a `next` field added (intrusive linked list pointer)
for this to work without extra allocation.

### `tachyon_go()` when there's no current proc context
```c
routine_id_t tachyon_go(void (*fn)(void*), void* arg) {
    routine_t* r = routine_create(fn, arg);
    if (tls_current_proc) {
        runq_push_owner(&tls_current_proc->local_queue, r);
    } else {
        /* called from somewhere with no proc context -- e.g. before
           runtime_init finished spawning loops, or from a raw pthread
           Tachyon doesn't know about */
        global_queue_push(&g_global_queue, r);
    }
    return r->id;
}
```

### `proc_run_loop` checks the global queue as a second fallback
```c
routine_t* r = runq_pop_owner(&p->local_queue);
if (!r) r = try_steal_from_others(p);
if (!r) r = global_queue_pop(&g_global_queue);   /* NEW */
if (!r) { nanosleep(...); continue; }
```

### Periodic proactive check (avoids only reacting when fully empty)
Go's scheduler checks the global queue every ~61 scheduler ticks even if
the local queue isn't empty, specifically to prevent global-queue routines
from being starved indefinitely by a proc that always has local work. Add
a simple tick counter to `proc_t` and mirror this:
```c
p->tick++;
if (p->tick % 61 == 0) {
    routine_t* r = global_queue_pop(&g_global_queue);
    if (r) runq_push_owner(&p->local_queue, r);
}
```

## Step-by-step

1. Add `routine_t* next` field to `routine_t` for the intrusive list.
2. Implement `global_queue.h`/`.c` (mutex + linked list is fine — this is
   intentionally the "slow path," don't over-engineer it).
3. Update `tachyon_go()` to fall back to the global queue when
   `tls_current_proc` is NULL.
4. Update `proc_run_loop`'s pop order: local → steal → global.
5. Add the periodic global-queue check (every ~61 ticks) so it's not
   purely reactive.

## Definition of done
- `fairness.test.c`: call `tachyon_go()` **before** any proc loop exists
  (i.e. before `pthread_create` has run for any proc, or by directly
  calling it from a raw `pthread_create`d thread that never called
  `tachyon_runtime_init` itself) and confirm the routine still eventually
  runs once the runtime starts up.
- A stress variant: keep one proc permanently busy with local work (a
  routine that keeps re-spawning itself) while continuously pushing
  routines to the global queue from another thread; assert those
  global-queue routines still complete within a bounded time, proving the
  periodic-check mechanism prevents starvation.
