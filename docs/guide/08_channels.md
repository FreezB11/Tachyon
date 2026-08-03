# Phase 8 — Channels

## Why this phase exists
Channels are the most recognizably "Go-like" feature you can add, and
they're a relatively thin layer once phase 7's mutex/condvar exist — this
is the payoff phase after a lot of infrastructure work.

## What to build

### `tachyon_chan_t`
```c
typedef struct tachyon_chan {
    tachyon_mutex_t   lock;
    tachyon_condvar_t not_full;
    tachyon_condvar_t not_empty;

    unsigned char* buf;         /* ring buffer, capacity * elem_size bytes */
    size_t         elem_size;
    size_t         capacity;    /* 0 = unbuffered (rendezvous) */
    size_t         head, tail, count;

    int            closed;
} tachyon_chan_t;
```

### Buffered send/recv (capacity > 0)
```c
void tachyon_chan_send(tachyon_chan_t* c, const void* val) {
    tachyon_mutex_lock(&c->lock);
    while (c->count == c->capacity && !c->closed) {
        tachyon_condvar_wait(&c->not_full, &c->lock);
    }
    if (c->closed) { tachyon_mutex_unlock(&c->lock); /* TODO: panic/abort, sending on closed chan */ return; }
    memcpy(c->buf + (c->tail * c->elem_size), val, c->elem_size);
    c->tail = (c->tail + 1) % c->capacity;
    c->count++;
    tachyon_condvar_signal(&c->not_empty);
    tachyon_mutex_unlock(&c->lock);
}

int tachyon_chan_recv(tachyon_chan_t* c, void* out) {
    tachyon_mutex_lock(&c->lock);
    while (c->count == 0 && !c->closed) {
        tachyon_condvar_wait(&c->not_empty, &c->lock);
    }
    if (c->count == 0 && c->closed) { tachyon_mutex_unlock(&c->lock); return 0; }
    memcpy(out, c->buf + (c->head * c->elem_size), c->elem_size);
    c->head = (c->head + 1) % c->capacity;
    c->count--;
    tachyon_condvar_signal(&c->not_full);
    tachyon_mutex_unlock(&c->lock);
    return 1;
}
```

### Unbuffered (capacity == 0) — true rendezvous
Unbuffered channels need the sender to block until a receiver is
*actively* ready to take the value (not just "there's room"), which the
buffered logic above doesn't quite capture at `capacity == 0` (since
`count == capacity` would always be true). Cleanest approach: special-case
`capacity == 0` with a small dedicated handshake state (`waiting_receiver`
flag + a slot for the in-flight value) rather than trying to force the
ring-buffer logic to cover it. Worth writing this as a genuinely separate
code path rather than a unified one — trying to unify them is a common
source of subtle bugs in channel implementations.

### `tachyon_chan_close`
```c
void tachyon_chan_close(tachyon_chan_t* c) {
    tachyon_mutex_lock(&c->lock);
    c->closed = 1;
    tachyon_condvar_broadcast(&c->not_empty);
    tachyon_condvar_broadcast(&c->not_full);
    tachyon_mutex_unlock(&c->lock);
}
```
Matches Go semantics: after close, pending buffered values can still be
received (`recv` returns 1) until drained, then subsequent `recv` calls
return 0 immediately.

## Step-by-step

1. Implement buffered channels first (`capacity > 0`), test thoroughly.
2. Implement unbuffered as its own path.
3. Implement `close()` semantics and drain-then-empty behavior.
4. Consider (optional, later): a `select`-like construct for waiting on
   multiple channels at once — this is significantly more complex (needs
   to atomically register interest across multiple channels' wait queues)
   and is reasonable to leave as a stretch goal beyond the core roadmap.

## Definition of done
- `chan.test.c`: classic producer/consumer — N producers send M items
  each into a buffered channel, 1 consumer drains and sums them; assert
  the sum matches expected total exactly.
- Unbuffered variant of the same test — must still pass, confirming the
  rendezvous path works, not just the buffered path.
- Close-semantics test: producers finish and close the channel; consumer
  loop (`while (tachyon_chan_recv(...))`) must terminate cleanly, not hang.
- Run all of the above under ThreadSanitizer.
