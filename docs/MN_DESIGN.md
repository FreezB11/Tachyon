# Tachyon — M:N Green Threading Library: Design Outline

_Companion to `STATUS.md` (current bugs/status) and `doc.md` (original notes).
This file is the target architecture for turning Tachyon into a Go-style
M:N runtime usable as a library in any project._

---

## The Goal

Many lightweight routines (the "M") multiplexed across a small number of
real OS threads (the "N"), with work stealing, real sync primitives, and a
clean public API — packaged as a library, not a source-drop.

---

## Target file tree

```
Tachyon/
├── include/
│   └── tachyon/                      # public headers — the ONLY dir consumers touch
│       ├── tachyon.h                 # top-level umbrella include
│       ├── routine.h                 # routine_t, routine states, spawn/exit API
│       ├── runtime.h                 # runtime init/shutdown, config
│       ├── sync.h                    # mutex, condvar, waitgroup (join replacement)
│       ├── chan.h                    # channels
│       └── export.h                  # visibility macros (TACHYON_API etc) for .so builds
│
├── src/
│   ├── core/
│   │   ├── routine.c / routine.h     # (was thread.c/thread.h) routine_t lifecycle
│   │   ├── runq.c / runq.h           # per-P work-stealing deque (NEW, phase 3)
│   │   ├── sched.c / sched.h         # per-P scheduler struct + loop (was global g_sched)
│   │   ├── proc.c / proc.h           # "P" = OS-thread-bound scheduling context (NEW)
│   │   ├── global_queue.c/.h         # injector queue for external spawns (NEW, phase 4)
│   │   ├── preempt.c / preempt.h     # signal-based preemption, per-P timers eventually
│   │   ├── mutex.c / condvar.c       # sync primitives (phase 6)
│   │   ├── chan.c                    # channel implementation (phase 6)
│   │   └── waitgroup.c               # replacement for busy-yield join
│   │
│   └── arch/
│       └── x86_64/
│           ├── context.h / context.S # unchanged — ctx_switch/ctx_save
│           └── cpu_count.c           # sysconf-based core detection helper
│
├── test/
│   ├── unit/
│   │   ├── routine_spawn.test.c
│   │   ├── routine_join.test.c       # via waitgroup, not busy loop
│   │   ├── yield.test.c
│   │   ├── preempt_single_os_thread.test.c
│   │   ├── mutex.test.c
│   │   ├── condvar.test.c
│   │   └── chan.test.c
│   ├── integration/
│   │   ├── multi_proc_spawn.test.c   # routines actually run across N OS threads
│   │   ├── work_stealing.test.c      # deliberately imbalance load, assert stealing happens
│   │   ├── preempt_multi_proc.test.c # busy-loop routine on one P, others still progress
│   │   └── fairness.test.c           # no routine starves under load
│   ├── stress/
│   │   ├── churn.test.c              # spawn/exit thousands of routines rapidly
│   │   └── race_stress.test.c        # repeat N times in CI to catch flaky races
│   └── bench/
│       ├── ctx_switch_bench.c        # already exists, keep
│       ├── spawn_bench.c
│       └── steal_bench.c
│
├── examples/                         # proves "usable in any project"
│   ├── standalone_lib_usage/         # a tiny separate project linking libtachyon.a
│   └── producer_consumer_chan/
│
├── docs/
│   ├── STATUS.md                     # living roadmap/status
│   ├── doc.md                        # original design notes
│   ├── MN_DESIGN.md                  # this file
│   └── API.md                        # generated/maintained public API reference
│
├── Makefile                          # add: `make lib`, `make install`, `make test-stress`
└── README.md
```

Key structural change: **`include/` becomes the only public surface**
(namespaced under `tachyon/`), and internal headers (`sched.h`, `runq.h`,
`proc.h`) move to live beside their `.c` files in `src/core/`, not in
`include/core/` like today. That's what makes it a real library instead of
a source-drop.

---

## Core data structures / API skeleton

### `runtime.h` — process-wide config
```c
typedef struct {
    int n_procs;          /* number of OS threads; 0 = auto (core count) */
    int max_routines;     /* soft cap, 0 = unbounded */
} tachyon_config_t;

int  tachyon_runtime_init(const tachyon_config_t* cfg);  /* NULL = defaults */
void tachyon_runtime_shutdown(void);
```

### `routine.h` — the "M" (replaces `thread_t`/`tachyon_thread`)
```c
typedef uint64_t routine_id_t;

typedef enum { R_READY, R_RUNNING, R_BLOCKED, R_DEAD } routine_state_t;

typedef struct routine routine_t;   /* opaque to consumers */

routine_id_t tachyon_go(void (*fn)(void*), void* arg);  /* spawn, Go-style name */
void         tachyon_yield(void);
routine_id_t tachyon_self(void);
void         tachyon_exit(void);    /* explicit exit, in addition to fn() returning */
```

### `proc.h` (internal) — the "N", one per OS thread
```c
typedef struct proc {
    pthread_t   os_thread;
    runq_t      local_queue;      /* work-stealing deque, this P's own */
    context_t   sched_ctx;        /* this P's scheduling loop resume point */
    routine_t*  current;
    timer_t     preempt_timer;
    int         id;
} proc_t;

extern proc_t* g_procs;           /* array, size = n_procs */
extern int     g_n_procs;
```

### `runq.h` (internal) — work-stealing deque
```c
typedef struct runq runq_t;

void       runq_init(runq_t* q);
void       runq_push_owner(runq_t* q, routine_t* r);   /* only the owning P calls this */
routine_t* runq_pop_owner(runq_t* q);                  /* only the owning P calls this */
routine_t* runq_steal(runq_t* q);                      /* any other P may call this */
```

### `sync.h` — replaces busy-yield join, adds real primitives
```c
typedef struct tachyon_mutex   tachyon_mutex_t;
typedef struct tachyon_condvar tachyon_condvar_t;
typedef struct tachyon_waitgroup tachyon_waitgroup_t;

void tachyon_mutex_init(tachyon_mutex_t*);
void tachyon_mutex_lock(tachyon_mutex_t*);
void tachyon_mutex_unlock(tachyon_mutex_t*);

void tachyon_condvar_wait(tachyon_condvar_t*, tachyon_mutex_t*);
void tachyon_condvar_signal(tachyon_condvar_t*);
void tachyon_condvar_broadcast(tachyon_condvar_t*);

void tachyon_waitgroup_add(tachyon_waitgroup_t*, int delta);
void tachyon_waitgroup_done(tachyon_waitgroup_t*);
void tachyon_waitgroup_wait(tachyon_waitgroup_t*);   /* blocks, not busy-yields */
```

### `chan.h` — Go-style channels
```c
typedef struct tachyon_chan tachyon_chan_t;

tachyon_chan_t* tachyon_chan_make(size_t elem_size, size_t capacity); /* 0 = unbuffered */
void tachyon_chan_send(tachyon_chan_t*, const void* val);
int  tachyon_chan_recv(tachyon_chan_t*, void* out);   /* returns 0 if channel closed */
void tachyon_chan_close(tachyon_chan_t*);
void tachyon_chan_free(tachyon_chan_t*);
```

---

## Test plan, mapped to phases

| Phase | Test file | What it proves |
|---|---|---|
| 0 | `preempt_single_os_thread.test.c` | busy-loop routine gets forcibly switched (current test, hardened) |
| 1 | `routine_spawn.test.c`, `routine_join.test.c` | renamed API works identically to today's behavior |
| 2 | `multi_proc_spawn.test.c` | spawn 100 routines with `n_procs=4`, assert routines actually execute on >1 OS thread (check `pthread_self()` inside routine) |
| 3 | `work_stealing.test.c` | flood one P's queue, leave others empty, assert idle Ps end up running some of that work (via counters, not timing) |
| 4 | `fairness.test.c` | spawn from "outside" (before any P exists) via global queue, assert it still runs |
| 5 | `preempt_multi_proc.test.c` | one P has a busy-loop routine that never yields; assert routines on *other* Ps still make progress |
| 6 | `mutex.test.c`, `condvar.test.c`, `chan.test.c` | classic producer/consumer, no data races (run under `-fsanitize=thread` in CI) |
| stress | `churn.test.c`, `race_stress.test.c` | spawn/exit thousands rapidly, repeat 100+ times, zero crashes |

General testing rules:
- Every concurrency test should be **assertion-based**, not print-and-eyeball (atomic counters checked at the end) — interleaving timing varies run to run.
- Add `-fsanitize=thread` and `-fsanitize=address` build variants (`make test-tsan`, `make test-asan`) — essential once real multi-OS-thread + shared-queue code exists.
- `race_stress.test.c` runs the same scenario 100+ times in a loop as one CI gate, since races are probabilistic.

---

## Makefile additions to plan for
```
make lib          # builds libtachyon.a and .so
make install       # installs headers to include/tachyon/, lib to /usr/local/lib
make test          # unit + integration
make test-stress   # repeats stress/ tests N times
make test-tsan     # rebuild + run under ThreadSanitizer
make bench         # runs benchmarks/
```

---

## Suggested order of attack
1. Rename `thread_t`/`tachyon_thread` → routine-oriented naming, restructure `include/` into `include/tachyon/`
2. Refactor `g_sched` from a single global into a per-`proc_t` struct
3. Add real pthread-based N workers, one local queue each (no stealing yet)
4. Add work-stealing deque
5. Add global injector queue + rebalancing
6. Generalize preemption to per-P timers
7. Add mutex/condvar/waitgroup, then channels
8. Package as a library (`make lib`, `make install`, examples/)
