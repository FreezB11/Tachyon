# Phase 9 — Packaging (usable in any project)

## Why this phase exists
Everything up to this point makes Tachyon *work*. This phase makes it
*consumable* — the difference between "a repo you have to copy files out
of" and "a library you link against."

## What to build

### Makefile targets
```makefile
lib: $(OBJS)
	ar rcs build/libtachyon.a $(OBJS)
	$(CC) -shared -fPIC -o build/libtachyon.so $(OBJS)

install: lib
	mkdir -p $(PREFIX)/include/tachyon $(PREFIX)/lib
	cp include/tachyon/*.h $(PREFIX)/include/tachyon/
	cp build/libtachyon.a build/libtachyon.so $(PREFIX)/lib/

test-tsan:
	$(MAKE) clean
	$(MAKE) test CFLAGS="$(CFLAGS) -fsanitize=thread -g -O1"

test-asan:
	$(MAKE) clean
	$(MAKE) test CFLAGS="$(CFLAGS) -fsanitize=address -g -O0"

test-stress:
	for i in $$(seq 1 100); do ./build/bin/race_stress || exit 1; done
```
`PREFIX` should default to something like `/usr/local`, overridable via
`make install PREFIX=/path`.

For the shared library build, remember every `.c` file needs `-fPIC` —
either build two separate object sets (static vs PIC) or just always
compile with `-fPIC` (small overhead, simplifies the Makefile — reasonable
default for a library-first project).

### `include/tachyon/export.h` — actually use it now
```c
#pragma once
#if defined(_WIN32)
  #define TACHYON_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define TACHYON_API __attribute__((visibility("default")))
#else
  #define TACHYON_API
#endif
```
Apply `TACHYON_API` to every public function declaration in
`include/tachyon/*.h`, and compile the library with
`-fvisibility=hidden` so only explicitly marked symbols are exported —
this keeps internal helper functions (`resume_into`, `runq_steal`, etc)
out of the library's public symbol table entirely.

### `examples/standalone_lib_usage/`
A genuinely separate mini-project (its own `Makefile`, doesn't reach into
Tachyon's source tree) that does:
```makefile
CFLAGS = -I/usr/local/include
LDFLAGS = -L/usr/local/lib -ltachyon -lpthread -lrt
```
This is the real proof that phase 1-8's work resulted in something usable
outside the repo — if this example doesn't build against an `make
install`ed copy, something in the public/private header split leaked
internal dependencies.

### `examples/producer_consumer_chan/`
A short, realistic example using `tachyon_go`, channels, and a waitgroup
together — this doubles as your primary "here's how you use it" reference
for the README.

### `docs/API.md`
A hand-maintained (or doc-comment-generated, if you want to add that
later) reference listing every public function from
`include/tachyon/*.h`, one section per header, with a one-line description
and a tiny usage snippet each. Keep it strictly to the public API — no
internal types.

### README update
Once packaging is done, the README should show:
1. A "quick start" (`tachyon_go`, `tachyon_waitgroup_wait`) code block.
2. Build/install instructions (`make lib && sudo make install`).
3. A link to `docs/API.md` and `docs/MN_DESIGN.md`.
4. Current status (link to `docs/STATUS.md`) so anyone reading it knows
   what's solid vs. still in progress.

## Step-by-step

1. Add `lib`/`install` Makefile targets.
2. Fill in `export.h`, apply `TACHYON_API` throughout public headers,
   compile with `-fvisibility=hidden`.
3. Build `examples/standalone_lib_usage/` against an installed copy (use a
   local `PREFIX` like `./local-install` during development so you don't
   need real root/sudo access while iterating).
4. Build `examples/producer_consumer_chan/`.
5. Write `docs/API.md`.
6. Rewrite the README's top section.

## Definition of done
- `make lib && make install PREFIX=$(pwd)/local-install` succeeds.
- `examples/standalone_lib_usage/` builds and runs correctly using
  *only* `-I$(pwd)/local-install/include -L$(pwd)/local-install/lib
  -ltachyon`, with zero references back into Tachyon's `src/`.
- `nm -D build/libtachyon.so` shows only intentionally-public symbols
  (spot-check that internal helpers like `resume_into` do NOT appear).
- README's quick-start snippet, copy-pasted fresh into a new file, compiles
  and runs as-is.
