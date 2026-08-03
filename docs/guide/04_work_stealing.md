# Phase 4 — Work Stealing (this is what makes it M:N)

## Why this phase exists
After phase 3, routines are pinned to whichever proc created them — if
proc 0 has 500 routines queued and proc 3 has zero, proc 3 sits idle while
proc 0's core is overloaded. That's N:N, not M:N. Work stealing is the
mechanism that lets an idle proc grab routines from a busy proc's queue,
which is the actual defining feature of an M:N scheduler (this is exactly
what Go's runtime, and Cilk before it, do).

This is the hardest phase in the whole roadmap because the run queue stops
being "only touched by one thread" and becomes a genuine concurrent data
structure — two different OS threads can touch the same proc's queue at
the same time (the owner popping from one end, a thief stealing from the
other).

## What to build

### The queue shape: a deque, not a plain circular queue
The owner (`proc_run_loop` on its own thread) pushes/pops from one end
(call it the "bottom" — most recently added routines, LIFO for the owner,
which is cache-friendly and matches Go's design). Thieves steal from the
other end (the "top" — oldest routines, FIFO for thieves, which reduces
contention with the owner since they're working from opposite ends).

```c
/* src/core/runq.h */
typedef struct runq {
    routine_t* buf[RUNQ_CAPACITY];
    _Atomic int top;      /* thieves increment this */
    _Atomic int bottom;   /* owner increments/decrements this */
} runq_t;

void       runq_init(runq_t* q);

/* Owner-only. Not safe to call from any thread except the proc that owns q. */
void       runq_push_owner(runq_t* q, routine_t* r);
routine_t* runq_pop_owner(runq_t* q);

/* Callable from ANY proc's thread, including a non-owner stealing from q. */
routine_t* runq_steal(runq_t* q);
```

### Algorithm: Chase-Lev deque (the standard choice here)
This is the same algorithm Go, Java's ForkJoinPool, and most work-stealing
runtimes use. Don't invent your own lock-based version first and rewrite
later — implement Chase-Lev directly, it's well-documented and the
concurrency reasoning is subtle enough that reinventing it is a common
source of bugs.

Key properties to preserve:
- `push`/`pop` (owner side) never need atomics stronger than what a single
  thread requires for its own bookkeeping, **except** where they interact
  with `top` (which thieves also touch).
- `steal` uses a CAS (compare-and-swap) on `top` to claim a routine,
  so two thieves racing for the same last item only one of them wins.
- The array itself needs to handle growth (a fixed `RUNQ_CAPACITY` is fine
  to start with — Go's own local run queues are fixed-size, e.g. 256, with
  overflow going to the global queue from phase 5, which is a reasonable
  simplification for a first version here too).

### `proc_run_loop` gains a steal step
```c
static void* proc_run_loop(void* arg) {
    proc_t* p = (proc_t*)arg;
    tls_current_proc = p;
    for (;;) {
        routine_t* r = runq_pop_owner(&p->local_queue);
        if (!r) {
            r = try_steal_from_others(p);   /* NEW */
        }
        if (!r) {
            struct timespec ts = {0, 1000000};
            nanosleep(&ts, NULL);
            continue;
        }
        p->current = r;
        r->state   = R_RUNNING;
        resume_into(&p->sched_ctx, r);
    }
}

static routine_t* try_steal_from_others(proc_t* self) {
    /* randomize the start index each call so procs don't all hammer
       proc 0 first every time -- a common, cheap fairness trick */
    int start = rand_r(&self->steal_rng_state) % g_n_procs;
    for (int i = 0; i < g_n_procs; i++) {
        int idx = (start + i) % g_n_procs;
        if (idx == self->id) continue;
        routine_t* r = runq_steal(&g_procs[idx].local_queue);
        if (r) return r;
    }
    return NULL;
}
```

## Step-by-step

1. Implement `runq.h`/`runq.c` with the Chase-Lev algorithm. Write it in
   isolation first, with its own tiny test that just pushes/pops/steals
   from multiple `pthread`s directly (no routines/context-switching
   involved yet) — proving the deque itself is correct before wiring it
   into the scheduler removes a whole axis of debugging complexity.
2. Replace `proc_t`'s plain circular queue (`queue[]`/`head`/`tail`/`count`)
   with a `runq_t local_queue` field.
3. Update `proc_push`/`proc_pop` (or fold them directly into
   `runq_push_owner`/`runq_pop_owner` — consider deleting the old wrapper
   names now that the deque has its own clear API).
4. Add `try_steal_from_others` and wire it into `proc_run_loop`.
5. Add a per-proc `unsigned steal_rng_state` field for `rand_r`.

## Definition of done
- Standalone `runq` test: N threads pushing/popping/stealing thousands of
  items concurrently, with a checksum or counter verifying every pushed
  item is popped/stolen exactly once, no duplicates, no drops. Run under
  ThreadSanitizer (`make test-tsan`) — this is exactly the kind of bug TSan
  is built to catch and eyeballing output will not be reliable enough.
- `work_stealing.test.c`: spawn 1000 routines all via `tachyon_go()` called
  from a single proc (so they all land in one proc's queue initially),
  with `n_procs = 4`. Each routine increments an atomic counter and exits
  immediately. Assert the total count is exactly 1000 (no lost/duplicated
  routines) AND assert (via the same `pthread_self()`-recording trick from
  phase 3) that multiple procs actually processed some of the load — i.e.
  stealing genuinely happened, not just proc 0 running everything anyway.
- Re-run `preempt_multi_proc.test.c` from phase 3 — should still pass;
  stealing shouldn't break existing preemption behavior.
