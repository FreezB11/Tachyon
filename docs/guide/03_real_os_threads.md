# Phase 3 — Real OS Threads (N:N, not yet M:N)

## Why this phase exists
This is where "N" actually becomes plural. After this phase, Tachyon runs
routines across multiple real OS threads — but each routine still stays
pinned to whichever `proc_t` created it, since there's no stealing yet.
That's technically N:N (each OS thread runs its own independent pool), not
M:N. Phase 4 (work stealing) is what turns this into true M:N. Doing this
split first, without stealing, isolates two hard problems instead of
tackling them simultaneously: "does multi-OS-thread execution work at all"
vs. "is the stealing algorithm correct."

## What to build

### `tachyon_config_t` + real `tachyon_runtime_init`
```c
typedef struct {
    int n_procs;   /* 0 = auto-detect via sysconf(_SC_NPROCESSORS_ONLN) */
} tachyon_config_t;

int tachyon_runtime_init(const tachyon_config_t* cfg);  /* cfg may be NULL for defaults */
void tachyon_runtime_shutdown(void);
```

### Each proc gets a real OS thread and a scheduling loop
```c
typedef struct proc {
    /* ...existing fields from phase 2... */
    pthread_t os_thread;
} proc_t;

static void* proc_run_loop(void* arg) {
    proc_t* p = (proc_t*)arg;
    tls_current_proc = p;              /* set the TLS pointer on THIS os thread */
    for (;;) {
        routine_t* r = proc_pop(p);
        if (!r) {
            /* nothing to do yet -- phase 3 has no stealing, so just spin/sleep
               briefly. This is intentionally dumb for now; phase 4 replaces
               it with stealing, phase 5 replaces the spin with a real
               injector-queue check. A short nanosleep here avoids pegging
               a CPU core at 100% doing nothing. */
            struct timespec ts = {0, 1000000}; /* 1ms */
            nanosleep(&ts, NULL);
            continue;
        }
        p->current = r;
        r->state   = R_RUNNING;
        resume_into(&p->sched_ctx, r);   /* same resume_into from tachyon.c today */
        /* control returns here once r yields/exits/is preempted back to
           the scheduler loop -- exactly mirroring how main() used to work
           as the implicit "P0" in phases 0-2 */
    }
    return NULL;
}
```
Note: `tachyon_go()` today immediately switches into the new routine and
blocks the caller until it yields. That model gets awkward with multiple
OS threads (which proc's caller are we blocking?). **This phase should
change `tachyon_go()` to just enqueue the routine onto the *calling*
thread's `tls_current_proc` queue and return immediately** — the routine
starts running whenever that proc's loop gets to it. This is a real
behavior change from today, and it's the correct Go-like semantics (`go
f()` doesn't block the caller). Flag this clearly in your commit — it's
the biggest single behavior change in the whole roadmap.

### Runtime init spawns the threads
```c
int tachyon_runtime_init(const tachyon_config_t* cfg) {
    int n = (cfg && cfg->n_procs > 0) ? cfg->n_procs : (int)sysconf(_SC_NPROCESSORS_ONLN);
    g_n_procs = n;
    g_procs   = calloc(n, sizeof(proc_t));

    for (int i = 0; i < n; i++) {
        proc_init(&g_procs[i], i);
    }

    /* Proc 0 is special: it runs on the CALLING thread (so main() itself
       becomes P0's loop, no extra pthread needed for it) -- OR spawn n
       pthreads for all n procs including 0, and have main() just submit
       work and later call a blocking "wait for shutdown" function. Pick
       ONE model and be consistent; the "main thread IS P0" model is
       simpler to reason about and matches what phases 0-2 already do. */
    tls_current_proc = &g_procs[0];
    ctx_save(&g_procs[0].sched_ctx);

    for (int i = 1; i < n; i++) {
        pthread_create(&g_procs[i].os_thread, NULL, proc_run_loop, &g_procs[i]);
    }
    return 0;
}
```

## Step-by-step

1. Add `pthread_t os_thread` to `proc_t`.
2. Write `proc_run_loop` as shown above (spin-wait version is fine for now).
3. Change `tachyon_go()` semantics: enqueue-and-return instead of
   immediate-switch. This means `tachyon_go()`'s return value (`routine_id_t`)
   is now available before the routine has necessarily started — that's
   correct and matches Go.
4. Change `tachyon_runtime_init` to accept `tachyon_config_t*`, default to
   core count, spawn `n-1` pthreads (P0 = caller thread).
5. `main()`'s job after `tachyon_runtime_init()` now becomes: submit work
   via `tachyon_go()`, then either run P0's own loop directly (if main
   should also process routines) or call a new blocking
   `tachyon_runtime_wait()` that runs P0's loop until told to stop.
   Decide which model fits your use case and document it.
6. `tachyon_runtime_shutdown()`: signal all proc loops to exit (a simple
   `volatile int shutdown_flag` checked each loop iteration is enough for
   now), `pthread_join` them all, free `g_procs`.

## Definition of done
- `multi_proc_spawn.test.c` (from the test plan in `MN_DESIGN.md`): spawn
  ~100 routines with `n_procs = 4`, have each routine record
  `pthread_self()` into a shared array (protected by a plain `pthread_mutex_t`
  for now — Tachyon's own mutex doesn't exist until phase 7), and assert at
  the end that **more than one distinct `pthread_self()` value appears**.
  That's the concrete, assertion-based proof that routines actually ran on
  more than one OS thread.
- No crashes across 50+ repeated runs of that test (OS-thread startup
  timing is inherently racy to observe, even though the underlying
  mechanism should be deteradministratically correct).
- Preemption (from phase 0) still works independently on each proc — a
  busy-looping routine on proc 2 shouldn't be able to starve proc 0's own
  routines, since they're on different OS threads entirely at this phase
  (this should "just work" for free once each proc has its own timer +
  handler, but explicitly test it).
