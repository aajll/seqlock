# API reference

`seqlock.h` declares the whole interface and includes `seqlock_conf.h`. The examples in this document compile together as one file after `#include "seqlock.h"`.

## Conventions

- One writer per cell. `store` must never run on two threads, or in a thread and an interrupt, for the same cell at once; that is undefined behaviour. Any number of readers may call `load` and `try_load` concurrently with each other and with the writer.
- The payload type must be trivially copyable: it is copied byte for byte, so a pointer member shares its target rather than copying it.
- `init` is for setup. Call it before any reader or the writer uses the cell, and never concurrently with them.
- No function allocates, makes a system call, or takes a lock. Every function checks its pointers; a NULL argument never dereferences.

## Typed cells

```c
#include <stdint.h>

typedef struct {
        int32_t position;
        int32_t velocity;
} axis_state_t;

SEQLOCK_DEFINE(axis_cell, axis_state_t)
```

`SEQLOCK_DEFINE(name, type)` defines the cell type `name_t` and four `static inline` functions, `name_init`, `name_store`, `name_load` and `name_try_load`. Use it at file scope, without a trailing semicolon. It rejects a zero-size `type` and a `type` aligned more strictly than `max_align_t` at compile time.

The cell holds the payload by value, so it can be static, on the stack, or a member of another structure. Its layout is part of the contract:

<!-- snippet: skip -->
```c
typedef struct {
        seqlock_hdr_t hdr;
        type payload;
} name_t;
```

For `axis_cell` above, the generated functions are:

```c
void axis_cell_init(axis_cell_t *cell);
void axis_cell_store(axis_cell_t *cell, const axis_state_t *in);
void axis_cell_load(const axis_cell_t *cell, axis_state_t *out);
bool axis_cell_try_load(const axis_cell_t *cell, axis_state_t *out,
                        uint32_t max_retries);
```

`name_init` sets the counter to zero and zeroes the payload, so a reader that runs before the first store reads an all-zero value. Calling it again resets the cell under the same setup-only rule. There is no destroy function, because the cell owns no resources.

`name_store` copies `*in` into the cell. It is wait-free: it finishes in a bounded number of steps whatever the readers do. It does nothing if `cell` or `in` is NULL.

`name_load` copies a consistent value into `*out`: the payload of one complete `store`, or the zeroed payload if no store has happened. It retries while a store overlaps the copy, and spins while a store is in progress, so it does not return if the writer stops part-way through a store; see [progress](design.md#progress). If `cell` is NULL and `out` is not, it zeroes `*out`.

`name_try_load` does the same with at most `max_retries` attempts, and returns `true` with `*out` filled when one succeeds. It returns `false` when the attempts run out, when `max_retries` is 0, or when `cell` or `out` is NULL. After a `false` return the contents of `*out` are unspecified, because a failed attempt may have copied a mixture of two values; branch on the return value, never on `*out`.

## Core functions

```c
void seqlock_core_init(seqlock_hdr_t *hdr, void *payload, size_t size);
void seqlock_core_store(seqlock_hdr_t *hdr, void *payload, const void *in,
                        size_t size);
void seqlock_core_load(const seqlock_hdr_t *hdr, const void *payload, void *out,
                       size_t size);
bool seqlock_core_try_load(const seqlock_hdr_t *hdr, const void *payload,
                           void *out, size_t size, uint32_t max_retries);
```

The generated functions forward to these with `&cell->hdr`, `&cell->payload` and `sizeof(type)`. Call them directly when the payload size is known only at run time. The contracts are the same, with `size` as one more argument that must not be zero:

- `seqlock_core_init` does nothing if `hdr` is NULL, and leaves the payload alone if `payload` is NULL or `size` is 0.
- `seqlock_core_store` does nothing if any pointer is NULL or `size` is 0.
- `seqlock_core_load` and `seqlock_core_try_load` treat a NULL pointer or a zero `size` as invalid. They zero `*out` when `out` is non-NULL and `size` is non-zero, and `seqlock_core_try_load` returns `false`.

`seqlock_hdr_t` holds the sequence counter, `seq`. Only the library should read or write it while the cell is in use.

## Configuration

Each option is a macro in `seqlock_conf.h` with a default that a `-D` definition replaces. [integration.md](integration.md#configuration) explains which ones must match between the library and its callers.

`SEQLOCK_COUNTER_TYPE` is the unsigned type of the sequence counter, `uint32_t` by default. A signed type fails to compile, and so does a type the target cannot access lock-free under the `__atomic` or C11 backend. A wider counter only lengthens the [wrap interval](design.md#counter-wrap).

`SEQLOCK_DEFAULT_MAX_RETRIES` is 1000 by default. The library does not use it; it gives every caller of `try_load` in a project one shared bound.

`SEQLOCK_CELL_ALIGNED`, 0 by default, aligns each generated cell to `SEQLOCK_CACHELINE` bytes, 64 by default, when non-zero. In an array of cells written by different cores, this keeps each cell's counter off its neighbours' cache lines. It needs `SEQLOCK_CACHELINE` to be an alignment the compiler supports for objects of that storage duration.

`SEQLOCK_USE_GNU_ATOMICS`, `SEQLOCK_USE_C11_ATOMICS` and `SEQLOCK_USE_NO_ATOMICS` force a backend. Define at most one, to any value; defining two fails to compile. Without one, GCC and Clang use the `__atomic` backend, other C11 compilers with atomics use the C11 backend, and the rest use the uniprocessor backend. [design.md](design.md#backends) describes each.

`SEQLOCK_CPU_RELAX()` runs once per reader retry. It is the `pause` instruction on x86 and `yield` on AArch64 under GCC and Clang, and does nothing elsewhere. Define it to a statement for another spin hint. It takes effect when `src/seqlock.c` is compiled.

`SEQLOCK_ATOMIC_PAYLOAD_COPY` selects the payload copy. At 0 it is `memcpy`, marked with `SEQLOCK_NO_SANITIZE_THREAD`; at 1 it copies one byte at a time with relaxed `__atomic` operations. The default is 1 when ThreadSanitizer is active and 0 otherwise. The value 1 needs GCC or Clang. [design.md](design.md#payload-copy) explains the choice.

`SEQLOCK_NO_SANITIZE_THREAD` is the attribute placed on the `memcpy` copy. It defaults to `__attribute__((no_sanitize("thread")))` where the compiler supports `no_sanitize`, and to nothing elsewhere.

## Recipes

### Bounded read

A caller that must not spin, such as a control loop with a deadline, reads with a bound and falls back to its last good value:

```c
static axis_cell_t axis;
static axis_state_t axis_last_good;

/* Returns the current state, or the last one read if the writer is busy. */
axis_state_t
axis_sample(void)
{
        axis_state_t s;

        if (axis_cell_try_load(&axis, &s, SEQLOCK_DEFAULT_MAX_RETRIES)) {
                axis_last_good = s;
        }
        return axis_last_good;
}
```

On a single core where the reader is an interrupt handler and the writer runs at a lower priority, this is the only safe way to read: the handler can interrupt a store, and the store cannot finish until the handler returns, so `load` would never return.

### One cell per channel

```c
#define CHANNEL_COUNT 8u

SEQLOCK_DEFINE(channel_cell, float)

static channel_cell_t channels[CHANNEL_COUNT];

void
channels_init(void)
{
        uint32_t i;

        for (i = 0u; i < CHANNEL_COUNT; ++i) {
                channel_cell_init(&channels[i]);
        }
}
```

Each channel has its own writer. If those writers run on different cores, build with `-DSEQLOCK_CELL_ALIGNED=1` so that a store to one cell does not invalidate the cache line a reader of the next cell is using.

### Run-time payload size

The core functions take the cell as separate counter and payload pointers, so the caller owns the storage and its size:

```c
#define RECORD_MAX_BYTES 64u

typedef struct {
        seqlock_hdr_t hdr;
        _Alignas(max_align_t) unsigned char bytes[RECORD_MAX_BYTES];
        size_t size;
} record_cell_t;

void
record_init(record_cell_t *cell, size_t size)
{
        cell->size = size; /* fixed before any reader or writer starts */
        seqlock_core_init(&cell->hdr, cell->bytes, cell->size);
}

void
record_store(record_cell_t *cell, const void *in)
{
        seqlock_core_store(&cell->hdr, cell->bytes, in, cell->size);
}

void
record_load(const record_cell_t *cell, void *out)
{
        seqlock_core_load(&cell->hdr, cell->bytes, out, cell->size);
}
```

`size` must not exceed `RECORD_MAX_BYTES` and must not change while the cell is in use, because the reader and the writer both copy that many bytes.
