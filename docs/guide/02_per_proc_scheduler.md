# Phase 2 — Per-Proc Scheduler

## Why this phase exists
A single global `g_sched` works for one OS thread. The moment phase 3 adds
real `pthread`s, multiple OS threads would all read/write the same global
run queue, `current` pointer, and `main_ctx` simultaneously — that's a data
race by construction, not just a bug waiting to happen. This phase
restructures the scheduler into a per-OS-thread struct *before* real OS
threads exist, so phase 3 only has to plug pthreads into an already-correct
shape, rather than debugging concurrency and restructuring at the same time.

Borrowing Go's terminology: each OS-thread-bound scheduling context is
called a **"P"** (`proc_t`). For now there will only ever be one `proc_t`
(created by `tachyon_runtime_init`), running on whatever thread called it
(main). Phase 3 is what actually spawns more of them.

## What to build

### `src/core/proc.h` / `proc.c` (new)
```c
typedef struct proc {
    int          id;
    routine_t*   queue[MAX_ROUTINES];   /* same circular queue as today's g_sched.queue */
    int          head, tail, count;
    routine_t*   current;               /* NULL = this P is running its own "main"/loop code */
    context_t    sched_ctx;             /* replaces today's g_sched.main_ctx, but now per-P */

    /* preemption bookkeeping, still per-P at this phase (timer itself
       stays global/single until phase 6) */
    ucontext_t   sched_preempt_uctx;
    int          sched_was_preempted;

    uint64_t     next_routine_id;
} proc_t;

void       proc_init(proc_t* p, int id);
void       proc_push(proc_t* p, routine_t* r);
routine_t* proc_pop(proc_t* p);
```
This is a mechanical rename+move of everything currently in `sched.h`/
`sched.c` (`sched_t`→`proc_t`, `sched_push`→`proc_push`, etc), with `main_ctx`
renamed to `sched_ctx` to reflect that every P (not just "main") has one —
main is just P0's caller in this phase.

### Global proc registry (still needed even with only 1 proc for now)
```c
extern proc_t* g_procs;     /* array, size g_n_procs — malloc'd in runtime_init */
extern int     g_n_procs;
extern __thread proc_t* tls_current_proc;  /* NEW: thread-local pointer */
```
The `__thread` (or `_Thread_local`) pointer is the important new concept
here: every function that today does `g_sched.xxx` needs to instead do
`tls_current_proc->xxx`. This is what makes the code correct once phase 3
adds real threads — each OS thread's `tls_current_proc` naturally points at
its own `proc_t`, with zero shared mutable state for the "which proc am I"
question specifically. (The queue *inside* each proc_t is still only
touched by its own thread until phase 4's stealing — that's fine for now.)

## Step-by-step

1. Rename `sched.h`/`sched.c` → `proc.h`/`proc.c`, rename all types/fields
   per above.
2. Add the `tls_current_proc` thread-local and set it once, in
   `tachyon_runtime_init()`, to `&g_procs[0]`.
3. Grep every use of `g_sched.` in `routine.c`/`preempt.c` and replace with
   `tls_current_proc->`.
4. Update `preempt.c`'s handler: it currently reads `g_sched.current`
   directly (fine, since the handler always runs on the same OS thread
   that was interrupted) — change to `tls_current_proc->current`. Since a
   signal handler runs on the same OS thread as whatever it interrupted,
   `tls_current_proc` is guaranteed correct inside the handler without any
   extra synchronization.
5. `tachyon_runtime_init()` should now:
   - allocate `g_procs` (size 1 for this phase),
   - call `proc_init(&g_procs[0], 0)`,
   - set `tls_current_proc`,
   - `ctx_save(&g_procs[0].sched_ctx)` (replaces the old `ctx_save(&g_sched.main_ctx)`).

## Definition of done
- All phase-1 tests still pass, unchanged in behavior — this phase is a
  pure refactor, no new capability yet.
- `grep -rn "g_sched"` returns nothing anywhere in `src/`.
- A single `proc_t` correctly plays the role the old global did; the
  program is still single-OS-thread at the end of this phase.
- Confirm via a quick manual check that `tls_current_proc` is genuinely
  `__thread`-qualified (not just a plain global that happens to work with
  one thread) — this is the detail phase 3 depends on being right.
