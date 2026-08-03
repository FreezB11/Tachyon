# Phase 7 — Sync Primitives (Waitgroup, Mutex, Condvar)

## Why this phase exists
`tachyon_join()` today busy-yields in a `while` loop until the target
routine is `DEAD` — wasteful even in the 1:N world, and actively harmful
in the M:N world (a proc stuck busy-yielding on a join can't do other
useful work, and burns a full core spinning). Real blocking primitives are
also a prerequisite for channels (phase 8), which need a wait queue
underneath.

## What to build

### Routine-level blocking primitive: a wait queue
Before building mutex/condvar/waitgroup individually, build one shared
mechanism they all use: a way to put the *current* routine to sleep
(`R_BLOCKED`) and wake it later from any proc.

```c
/* src/core/wait.h (internal) */
typedef struct wait_node {
    routine_t*        r;
    struct wait_node*  next;
} wait_node_t;

typedef struct wait_queue {
    pthread_mutex_t lock;   /* protects the list itself; short critical sections only */
    wait_node_t*    head;
    wait_node_t*    tail;
} wait_queue_t;

void wait_queue_init(wait_queue_t* q);
void wait_queue_park(wait_queue_t* q);     /* blocks calling routine, enqueues it, yields */
void wait_queue_wake_one(wait_queue_t* q); /* wakes the oldest parked routine, if any */
void wait_queue_wake_all(wait_queue_t* q);
```
`wait_queue_park()` internally: mark `tls_current_proc->current->state =
R_BLOCKED`, append to the wait queue's list, then yield to the scheduler
(similar to today's `tachyon_yield`, except the routine does **not** get
re-pushed to any run queue — it only becomes runnable again when
`wait_queue_wake_one/all` explicitly re-enqueues it onto some proc's
`runq` or the global queue).

### `tachyon_waitgroup_t` (replaces busy-yield join)
```c
typedef struct tachyon_waitgroup {
    _Atomic int  count;
    wait_queue_t waiters;
} tachyon_waitgroup_t;

void tachyon_waitgroup_add(tachyon_waitgroup_t* wg, int delta) {
    atomic_fetch_add(&wg->count, delta);
}
void tachyon_waitgroup_done(tachyon_waitgroup_t* wg) {
    if (atomic_fetch_sub(&wg->count, 1) == 1) {
        wait_queue_wake_all(&wg->waiters);
    }
}
void tachyon_waitgroup_wait(tachyon_waitgroup_t* wg) {
    while (atomic_load(&wg->count) > 0) {
        wait_queue_park(&wg->waiters);
    }
}
```
Usage replaces the old join pattern:
```c
tachyon_waitgroup_t wg = {0};
tachyon_waitgroup_add(&wg, 1);
tachyon_go(worker_wrapper, &wg);   /* worker calls tachyon_waitgroup_done(&wg) before exiting */
tachyon_waitgroup_wait(&wg);
```
Keep `tachyon_join(routine_id_t)` around too if you like the ergonomics —
it can be implemented as a thin wrapper: a small internal waitgroup-per-routine
that `routine_exit` signals automatically. Either is fine; document
whichever you pick as the primary pattern.

### `tachyon_mutex_t`
```c
typedef struct tachyon_mutex {
    _Atomic int  locked;   /* 0 or 1 */
    wait_queue_t waiters;
} tachyon_mutex_t;

void tachyon_mutex_lock(tachyon_mutex_t* m) {
    int expected = 0;
    while (!atomic_compare_exchange_weak(&m->locked, &expected, 1)) {
        expected = 0;
        wait_queue_park(&m->waiters);
    }
}
void tachyon_mutex_unlock(tachyon_mutex_t* m) {
    atomic_store(&m->locked, 0);
    wait_queue_wake_one(&m->waiters);
}
```

### `tachyon_condvar_t`
Standard textbook shape, parking on its own wait queue and requiring the
caller to hold the associated mutex:
```c
typedef struct tachyon_condvar {
    wait_queue_t waiters;
} tachyon_condvar_t;

void tachyon_condvar_wait(tachyon_condvar_t* cv, tachyon_mutex_t* m) {
    tachyon_mutex_unlock(m);
    wait_queue_park(&cv->waiters);
    tachyon_mutex_lock(m);
}
void tachyon_condvar_signal(tachyon_condvar_t* cv)   { wait_queue_wake_one(&cv->waiters); }
void tachyon_condvar_broadcast(tachyon_condvar_t* cv) { wait_queue_wake_all(&cv->waiters); }
```

## Step-by-step

1. Add `R_BLOCKED` handling to the scheduler loop if not already fully
   wired (it exists as an enum value already, per `thread.h`/`routine.h`,
   but check it's actually respected everywhere — a `BLOCKED` routine must
   never be picked up by `runq_pop_owner`/`runq_steal` while parked, only
   once explicitly woken).
2. Implement `wait_queue_t` and its three functions.
3. Implement waitgroup, mutex, condvar on top of it, in that order (each
   is a good standalone test case before the next depends on it working).
4. Convert `tachyon_join`'s implementation (or deprecate it) in favor of
   waitgroup-based joining.

## Definition of done
- `mutex.test.c`: many routines across many procs incrementing a shared
  (non-atomic, deliberately) counter protected by the mutex; final count
  must exactly match expected total. Run under ThreadSanitizer.
- `condvar.test.c`: classic producer/consumer with a bounded buffer,
  correct signaling, no missed wakeups (run repeatedly, assert completion
  within a timeout — a missed-wakeup bug manifests as a hang).
- Confirm `wait_queue_park` genuinely removes the routine from being
  runnable — a stress test where many routines contend for one mutex
  should not show 100% CPU spinning across all cores (that would indicate
  parking degenerated back into busy-waiting).
