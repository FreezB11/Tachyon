# Phase 1 — Rename & Restructure

## Why this phase exists
Every later phase adds new public types (`proc_t`, `runq_t`, channels,
mutexes). If the public API still says "thread" while OS-thread-backed
`proc_t`s also exist, "thread" becomes ambiguous — does it mean a routine
or a real OS thread? Renaming now, while the API surface is still small,
avoids a much bigger rename later. This phase also turns `include/` into a
proper namespaced public API instead of a flat header dump.

## What to build

### New public header layout
```
include/tachyon/
├── tachyon.h      # umbrella include (what consumers actually #include)
├── routine.h      # was: tachyon.h's thread declarations + core/thread.h's public bits
├── runtime.h       # NEW — init/shutdown, currently doesn't exist (tachyon_init macro today)
└── export.h        # NEW — empty for now, placeholder for .so visibility macros later
```

Internal headers move out of `include/` entirely and sit next to their
`.c` files:
```
src/core/
├── routine.c / routine.h   # was thread.c / include/core/thread.h
├── sched.c / sched.h       # was sched.c / include/core/sched.h
└── preempt.c / preempt.h   # was preempt.c / include/core/preempt.h
```
`src/arch/x86_64/context.h` stays where it is — it's used by both public
and internal code, but it's low-level enough that it doesn't need to be
public. Keep it under `src/arch/`, included via relative path.

### Rename table

| Old | New |
|---|---|
| `thread_t` | `routine_t` |
| `thread_t.tid` | `routine_t.id` |
| `t_create()` | `routine_create()` |
| `t_destroy()` | `routine_destroy()` |
| `t_init()` | `routine_init()` |
| `tachyon_thread()` | `tachyon_go()` |
| `tachyon_spool()` | `tachyon_go_n()` (spawn n copies) |
| `tachyon_yield()` | unchanged — already a good name |
| `tachyon_join()` | keep for now, but note it'll be replaced by `tachyon_waitgroup_wait()` in phase 7 |
| `tachyon_thread_exit()` | `tachyon_exit()` |
| `tachyon_self()` | unchanged |
| `State` enum (`READY`/`RUNNING`/`BLOCKED`/`DEAD`) | `routine_state_t` (`R_READY`/`R_RUNNING`/`R_BLOCKED`/`R_DEAD`) — prefixed so it doesn't collide with future `proc_state_t` |
| `g_sched` | stays for this phase (per-proc split happens in phase 2) — just update its field types if `thread_t*` appears in it |

### `tachyon_init()` macro → real function
Today:
```c
#define tachyon_init() \
    do { _tachyon_init(); ctx_save(&g_sched.main_ctx); } while(0)
```
Replace with a real function in the new `runtime.h`:
```c
int tachyon_runtime_init(void);   /* returns 0 on success */
```
The `ctx_save(&g_sched.main_ctx)` call it used to do inline should move
inside this function's implementation — no reason for it to be a macro
once it's a real library entry point. Note: in phase 2, this signature
changes again to take a config struct — that's fine, don't over-build it
now.

## Step-by-step

1. Create `include/tachyon/` and move+rename headers per the table above.
2. Create `src/core/routine.h` (internal — includes `context.h`, defines
   `routine_t` fully). The *public* `include/tachyon/routine.h` should only
   declare the opaque type and the functions consumers call
   (`tachyon_go`, `tachyon_yield`, `tachyon_self`, `tachyon_exit`) — not the
   struct internals. This split (public opaque type vs. internal full
   definition) is what makes it a real library API instead of leaking
   implementation details.
3. Rename `thread.c` → `routine.c`, update all function/type names inside.
4. Update `tachyon.c` (or split it — see note below) to use the new names.
5. Update `main.c` and everything in `test/` to use the new API names.
6. Update the `Makefile`'s include paths (`-Iinclude` → still works if
   consumers `#include <tachyon/tachyon.h>`, but double check `-I` flags
   point at `include/`, not `include/tachyon/`, so the namespaced include
   path works correctly).
7. Grep the whole repo for `thread_t`, `tachyon_thread`, `t_create`,
   `t_destroy`, `State`, `READY`/`RUNNING`/`BLOCKED`/`DEAD` to make sure
   nothing was missed.

### Optional: split `tachyon.c`
`tachyon.c` currently holds init, spawn, yield, exit, join, self all in one
file. Once renamed, consider splitting into `src/core/routine.c` (spawn,
yield, exit, self — routine lifecycle) and a later `src/core/sched.c`
(scheduler-loop-specific logic once phase 2 introduces a real per-proc
loop). Not mandatory for phase 1, but easier to do now than after phase 2
adds more code to the same file.

## Definition of done
- Every existing test (`thread_join`, `yield`, `spool_yield`, `preempt`)
  still compiles and passes, just calling the renamed functions.
- `grep -rn "thread_t\|tachyon_thread\b" include/ src/ test/ main.c` returns
  nothing.
- `include/core/` no longer exists; all internal headers live in `src/core/`.
- A consumer file doing only `#include <tachyon/tachyon.h>` (with
  `-Iinclude`) can call `tachyon_runtime_init()`, `tachyon_go()`,
  `tachyon_yield()`, `tachyon_self()` and nothing else leaks through.
