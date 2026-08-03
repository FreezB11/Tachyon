# Phase 6 — Preemption Across Many Procs

## Why this phase exists
The existing preemption mechanism (from phase 0 / `STATUS.md`) was built
for a single OS thread: one `SIGALRM` timer, one handler, one implicit
"current running thing." With multiple real procs (phase 3+), each
running on its own OS thread, a single process-wide `SIGALRM` would be
delivered to *some* thread (Linux picks one arbitrarily among those that
don't have it blocked) — not necessarily the one you want interrupted.
This phase makes preemption per-proc-aware.

## What to build

### Option A (recommended): `timer_create` with `SIGEV_THREAD_ID`
Linux supports creating a POSIX timer that delivers its signal to a
**specific thread** (not just the process), via the non-portable but
well-supported `sigevent.sigev_notify = SIGEV_THREAD_ID` and
`sigev_notify_thread_id = gettid()`. Each proc creates its own timer
targeted at its own OS thread's tid.

```c
/* in proc_run_loop, right after tls_current_proc = p; */
struct sigevent sev;
memset(&sev, 0, sizeof(sev));
sev.sigev_notify          = SIGEV_THREAD_ID;
sev.sigev_signo           = SIGALRM;
sev.sigev_notify_thread_id = (pid_t) syscall(SYS_gettid);
timer_create(CLOCK_MONOTONIC, &sev, &p->preempt_timer);

struct itimerspec its = { /* same as today's PREEMPT_INTERVAL_MS setup */ };
timer_settime(p->preempt_timer, 0, &its, NULL);
```
This requires `#include <sys/syscall.h>` for `SYS_gettid` (glibc didn't
expose a wrapper for `gettid()` directly until fairly recent versions —
check what's available in your toolchain and fall back to the syscall if
needed).

### Option B (portable fallback): one shared timer + `pthread_kill`
If `SIGEV_THREAD_ID` isn't available/desired, a simpler portable approach:
one central timer thread wakes up every `PREEMPT_INTERVAL_MS`, iterates
`g_procs`, and calls `pthread_kill(p->os_thread, SIGALRM)` for each. Less
elegant, adds one extra always-running housekeeping thread, but avoids the
Linux-specific `SIGEV_THREAD_ID` mechanism. Pick Option A unless you have
a portability requirement that rules it out.

### The handler itself: mostly unchanged, but reads `tls_current_proc`
The core `scheduler_interrupt` logic from `STATUS.md`/today's `preempt.c`
carries over almost as-is — it already needs to answer "who is running,
what do I switch to." The only change: replace every `g_sched.` reference
with `tls_current_proc->` (this should already be done as part of phase 2
if you followed that guide, so this phase may just be re-confirming it's
still correct, plus wiring up the per-proc timer creation).

### The "switching_to_main" fallback complicates slightly
In the single-proc version, "queue empty, current is a real routine" meant
"fall back to whoever's frozen in `ctx_switch`, i.e. main/the scheduler
loop." In the per-proc world, that's still correct — it just means
falling back to `p->sched_ctx` (the proc's own scheduling loop resume
point) instead of a global `main_ctx`. Since phase 2 already renamed
`main_ctx` → `sched_ctx` per-proc, this should fall out naturally rather
than needing new logic.

## Step-by-step

1. Move timer creation from a single global `preempt_init()` call into
   each proc's startup (inside `proc_run_loop`, before entering the loop).
2. Decide Option A vs B above and implement it.
3. Confirm/update every `g_sched.` reference in `preempt.c` is
   `tls_current_proc->` (should mostly be done via phase 2, this is a
   verification pass specific to preemption code).
4. Update `preempt_disable`/shutdown path: each proc must delete its own
   timer during `tachyon_runtime_shutdown()`.
5. Re-run the known correctness issues noted in `STATUS.md` (the masking
   race, the `switching_to_main` FPU/sigmask gap) — these bugs, if still
   unresolved, now exist **per-proc**, meaning any stress test needs to
   account for N procs' worth of race surface, not just one. Worth fully
   resolving those before this phase rather than after, since debugging
   the same class of race across multiple threads simultaneously is much
   harder than debugging it on one.

## Definition of done
- `preempt_multi_proc.test.c`: one proc runs a busy-loop routine that
  never yields; routines on *every other* proc should still make measurable
  progress (assert via per-proc atomic counters that increment each
  routine iteration, checked after a fixed wall-clock window).
- Run the full stress suite (`race_stress.test.c` equivalent) under
  ThreadSanitizer with preemption enabled — this is the hardest
  correctness bar in the whole project, since it combines real threads,
  signal handlers, and manual context switching all at once.
