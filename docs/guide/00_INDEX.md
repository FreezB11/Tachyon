# Tachyon M:N Guide — Index

This is your working guide for turning Tachyon into a Go-style M:N
green-threading library. Each phase is its own file so you can tackle them
one at a time without losing the thread (pun intended).

Read `../MN_DESIGN.md` first if you haven't — it has the target file tree,
full API skeleton, and test plan in one place. These guide files break that
same plan into step-by-step instructions per phase.

## Reading order

1. [`01_rename_and_restructure.md`](./01_rename_and_restructure.md) —
   rename `thread_t`→`routine_t`, move headers into `include/tachyon/`.
   **Do this first**, everything else assumes it's done.
2. [`02_per_proc_scheduler.md`](./02_per_proc_scheduler.md) —
   turn the single global `g_sched` into a per-`proc_t` struct.
3. [`03_real_os_threads.md`](./03_real_os_threads.md) —
   spawn N pthreads, one local run queue each. No stealing yet.
4. [`04_work_stealing.md`](./04_work_stealing.md) —
   the actual M:N part — a thread-safe stealing deque.
5. [`05_global_queue.md`](./05_global_queue.md) —
   injector queue + load rebalancing.
6. [`06_preemption_per_proc.md`](./06_preemption_per_proc.md) —
   generalize the existing signal-based preemption to many OS threads.
7. [`07_sync_primitives.md`](./07_sync_primitives.md) —
   real blocking join (waitgroup), mutex, condvar.
8. [`08_channels.md`](./08_channels.md) —
   Go-style channels on top of the mutex/condvar work.
9. [`09_packaging.md`](./09_packaging.md) —
   `make lib`, `make install`, examples/, so it's usable from any project.

## How to use these

Each phase file has the same shape:
- **Why this phase exists** (what it unlocks)
- **What to build** (concrete file/function list)
- **Step-by-step instructions**
- **Definition of done** (what test(s) should pass before moving on)

Do not skip ahead — phase 3 (real OS threads) is meaningless without phase 2
(per-proc scheduler) done first, since a global `g_sched` can't be shared
safely across real OS threads. The dependency chain is strict up through
phase 4; phases 6-9 can be reordered a bit if you want channels before
hardened preemption, for example.

## Where the current known bugs live

Preemption correctness issues found while exploring this (a masking race,
and a rarer signal-mask/FPU-state issue under `switching_to_main`) are
tracked separately in `../STATUS.md`, not in these phase docs. Fix or accept
those before phase 6 (which generalizes preemption further and would
otherwise multiply the same bugs across every OS thread).
