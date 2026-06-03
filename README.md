# seqlock

[![CI](https://github.com/aajll/seqlock/actions/workflows/ci.yml/badge.svg)](https://github.com/aajll/seqlock/actions/workflows/ci.yml)

Single-writer / multi-reader sequence lock for lock-free, internally consistent snapshots of a multi-word value in C.

## Features

- **Lock-free readers, wait-free writer**: readers never block the writer and the writer never blocks on readers, so a slow or stalled consumer cannot stall the producer
- **Consistent multi-word snapshots**: a reader always observes either the complete previous value or the complete new one, never a torn tuple, even when the payload is wider than a machine word
- **Typed, zero-allocation cells**: `SEQLOCK_DEFINE(name, type)` generates a type-safe cell that stores the payload by value, with no heap, no syscalls, and no blocking on any path
- **Type-erased core**: the counter/fence/copy protocol is compiled once in `seqlock.c`; the typed API is a thin `static inline` shim, keeping the correctness-critical logic in one auditable place
- **Portable C11 atomics**: correct on weakly-ordered CPUs (AArch64) and x86-64, selecting a GCC/Clang `__atomic` path, a C11 `<stdatomic.h>` path, or a uniprocessor fallback
- **ThreadSanitizer-clean**: auto-selects a race-free relaxed-atomic payload copy under TSAN and a fast benign-race-annotated `memcpy` otherwise
- **Bounded read variant**: `try_load()` gives up after a caller-set retry count instead of spinning, for callers that must not block
- **Configurable**: counter width, per-cell cache-line alignment, retry default, and copy strategy are all overridable through `seqlock_conf.h`
- **Compliance-aware design goals**: static allocation, explicit per-function contracts, compile-time validation, and a small auditable codebase targeting IEC 61508 and MISRA C:2012 principles

## Using the Library

### As a Meson subproject

```meson
seqlock_dep = dependency('seqlock', fallback: ['seqlock', 'seqlock_dep'])
```

The project also exports `meson.override_dependency('seqlock', ...)`
so downstream Meson builds can resolve the subproject dependency by name.

For subproject builds, include the public header directly:

```c
#include "seqlock.h"
```

`seqlock.h` includes `seqlock_conf.h` automatically, so a single include is
enough. To pin configuration in one place, include `seqlock_conf.h` first:

```c
#define SEQLOCK_COUNTER_TYPE uint64_t
#include "seqlock_conf.h"
#include "seqlock.h"
```

### Project-level configuration

Every option in `seqlock_conf.h` is `#ifndef`-guarded, so a consumer can
override it without forking the library. Place a `config/seqlock_conf.h` in your
project, put `config/` ahead of the subproject include path, and your header
wins while the shipped defaults remain the fallback (the same pattern used for
`queue_conf.h` in the supervisory controller).

### As an installed dependency

If the library is installed system-wide, include the namespaced header path:

```c
#include <seqlock/seqlock.h>
```

The public configuration header is also installed as:

```c
#include <seqlock/seqlock_conf.h>
```

If `pkg-config` files are installed in your environment, downstream builds can
also discover the package as `seqlock`.

The generated version header is available as `seqlock_version.h` in the
build tree and as `<seqlock/seqlock_version.h>` after install. It is optional
and is not included by the library or its tests.

## Building

```sh
# Library only (release)
meson setup build --buildtype=release -Dbuild_tests=false
meson compile -C build

# With unit tests
meson setup build --buildtype=debug -Dbuild_tests=true
meson compile -C build
meson test -C build --verbose

# Concurrency check under ThreadSanitizer (x86-64 host; see Notes)
meson setup build-tsan --buildtype=debug -Dbuild_tests=true -Db_sanitize=thread
meson test -C build-tsan --verbose
```

## Quick Start

```c
#include "seqlock.h"

#include <stdbool.h>
#include <stdint.h>

/* The value published by one producer and read by many consumers. */
typedef struct {
        float    value;
        uint64_t timestamp_ns;
        bool     valid;
} io_sample_t;

/* Generate the typed cell `io_cache_t` and its init/store/load/try_load ops. */
SEQLOCK_DEFINE(io_cache, io_sample_t)

static io_cache_t cache; /* one cell; embed it anywhere, no heap needed */

/* --- producer side (sole writer) --- */
void publish(float v, uint64_t ts, bool ok)
{
        io_sample_t s = { .value = v, .timestamp_ns = ts, .valid = ok };
        io_cache_store(&cache, &s); /* wait-free */
}

/* --- consumer side (any number of readers) --- */
bool sample_is_fresh(uint64_t now_ns, uint64_t max_age_ns)
{
        io_sample_t s;
        io_cache_load(&cache, &s); /* lock-free, always a consistent tuple */
        return s.valid && (now_ns - s.timestamp_ns) <= max_age_ns;
}

int main(void)
{
        io_cache_init(&cache); /* setup-only: even counter, zeroed payload */
        publish(3.5f, 1000u, true);

        io_sample_t s;
        io_cache_load(&cache, &s);
        /* s holds {3.5f, 1000, true} as one consistent snapshot */
        return 0;
}
```

## API Reference

### Configuration Macros

| Macro                                                                            | Description                                                                                                                                                                                                                                                                                                  |
| -------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `SEQLOCK_COUNTER_TYPE`                                                           | Unsigned integer type of the sequence counter (default: `uint32_t`; set `uint64_t` to remove the theoretical wrap concern). Must be lock-free on the target                                                                                                                                                  |
| `SEQLOCK_DEFAULT_MAX_RETRIES`                                                    | Suggested project-wide default bound for `try_load()` callers (default: 1000)                                                                                                                                                                                                                                |
| `SEQLOCK_CELL_ALIGNED`                                                           | When non-zero, pad each generated cell to a cache line to avoid false sharing in arrays of cells (default: 0)                                                                                                                                                                                                |
| `SEQLOCK_CACHELINE`                                                              | Cache-line size in bytes used when `SEQLOCK_CELL_ALIGNED` is set (default: 64)                                                                                                                                                                                                                               |
| `SEQLOCK_ATOMIC_PAYLOAD_COPY`                                                    | Payload-copy strategy: `0` = benign-race annotated `memcpy`, `1` = race-free relaxed-atomic copy. Auto-selects `1` under ThreadSanitizer, `0` otherwise                                                                                                                                                      |
| `SEQLOCK_USE_GNU_ATOMICS` / `SEQLOCK_USE_C11_ATOMICS` / `SEQLOCK_USE_NO_ATOMICS` | Advanced backend selection override. Define exactly one consistently for the seqlock library build and all consumers; do not override these only in an application using an already-built library. Otherwise the library auto-selects GCC/Clang `__atomic`, then C11 atomics, then the uniprocessor fallback |
| `SEQLOCK_NO_SANITIZE_THREAD`                                                     | Attribute used on the benign-race payload-copy helper when `SEQLOCK_ATOMIC_PAYLOAD_COPY == 0`. Override only when the compiler needs a different annotation or none                                                                                                                                          |
| `SEQLOCK_CPU_RELAX`                                                              | Spin hint run on each reader retry (default: `pause` on x86, `yield` on AArch64, nothing elsewhere). Override to supply a platform relax primitive                                                                                                                                                           |

### Single-Include Override

Define macros before including `seqlock.h`; it pulls in the configuration
header automatically:

```c
#define SEQLOCK_COUNTER_TYPE uint64_t
#define SEQLOCK_CELL_ALIGNED 1
#include "seqlock.h"
```

### Type Definitions

| Type            | Description                                                                                                                               |
| --------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| `seqlock_hdr_t` | Sequence-counter header; the first member of every generated cell                                                                         |
| `name##_t`      | Generated by `SEQLOCK_DEFINE(name, type)`: a cell holding one `type` payload by value, laid out as `{ seqlock_hdr_t hdr; type payload; }` |

### Typed API

`SEQLOCK_DEFINE(name, type)` generates a typed cell and four operations. Each
forwards to the type-erased core and carries its contract: `store` is
single-producer, `load` / `try_load` serve any number of readers, and `type`
must be trivially copyable POD. Every operation validates its pointers.

```c
SEQLOCK_DEFINE(name, type)

void name##_init (name##_t *cell);                  /* setup-only */
void name##_store(name##_t *cell, const type *in);  /* sole producer, wait-free */
void name##_load (const name##_t *cell, type *out); /* any readers, lock-free */
bool name##_try_load(const name##_t *cell, type *out, uint32_t max_retries);
```

### Type-Erased Core

The typed shims forward to these; a caller that prefers runtime sizing over the
macro can use them directly.

```c
void seqlock_core_init (seqlock_hdr_t *hdr, void *payload, size_t size);
void seqlock_core_store(seqlock_hdr_t *hdr, void *payload,
                        const void *in, size_t size);
void seqlock_core_load (const seqlock_hdr_t *hdr, const void *payload,
                        void *out, size_t size);
bool seqlock_core_try_load(const seqlock_hdr_t *hdr, const void *payload,
                           void *out, size_t size, uint32_t max_retries);
```

## Use Cases

- **Latest-known-value cache cell**: one bus worker publishes the newest sample, the control and monitor threads read whatever the current value is

  ```c
  SEQLOCK_DEFINE(io_cache, io_sample_t)
  io_cache_store(&cell, &sample); /* producer */
  io_cache_load(&cell, &out);     /* any consumer */
  ```

- **Per-channel array without false sharing**: enable cache-line alignment so neighbouring cells written by different cores do not thrash a shared line

  ```c
  #define SEQLOCK_CELL_ALIGNED 1
  #include "seqlock.h"
  SEQLOCK_DEFINE(io_cache, io_sample_t)
  static io_cache_t channels[NUM_CHANNELS];
  ```

- **Non-spinning read on a deadline-sensitive path**: bound the retry count and fall back instead of spinning under a hot writer

  ```c
  io_sample_t out;
  if (io_cache_try_load(&cell, &out, SEQLOCK_DEFAULT_MAX_RETRIES)) {
          use(&out);
  } else {
          use_last_good(); /* out is unspecified on false */
  }
  ```

- **Eliminate the theoretical counter wrap**: widen the counter for consumers that want the concern gone entirely
  ```c
  #define SEQLOCK_COUNTER_TYPE uint64_t
  #include "seqlock.h"
  ```

## Notes

| Topic                                  | Note                                                                                                                                                                                                                                                                                                                                                                                            |
| -------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Single producer**                    | At most one thread may call `store()` on a given cell; concurrent writers are undefined behaviour. Use a write-side lock or a different primitive for multi-producer                                                                                                                                                                                                                            |
| **Readers**                            | `load()` and `try_load()` are safe for any number of concurrent readers, concurrent with the single writer                                                                                                                                                                                                                                                                                      |
| **Writer suspended mid-write**         | A slow or hung producer between stores leaves the counter even, so `load()` returns the last stable snapshot immediately (freedom from interference holds). The only blocking window is if the writer is suspended or killed _between_ its two increments (the brief odd window); a reader then spins until it resumes. For a hard bound even in that pathological case, read with `try_load()` |
| **Reader progress under a hot writer** | A continuously-storing writer can force `load()` to retry; it still makes progress (not a hard livelock) but read throughput drops. `try_load()` bounds the work; `SEQLOCK_CPU_RELAX` eases contention while spinning                                                                                                                                                                           |
| **POD payload**                        | The payload must be trivially copyable (no pointers needing a deep copy, no self-references); it is copied by value                                                                                                                                                                                                                                                                             |
| **`init()` is setup-only**             | Must not run concurrently with `store()` / `load()` on the same cell. It is idempotent; there is no destroy/reset because the cell owns no resources                                                                                                                                                                                                                                            |
| **`try_load()` failure**               | On `false` the contents of `*out` are unspecified (the no-allocation rule precludes a scratch buffer); branch on the return value, never on `*out`                                                                                                                                                                                                                                              |
| **No dynamic memory**                  | No heap allocation, no syscalls, no blocking; cells are caller-owned and can be stack- or statically-allocated                                                                                                                                                                                                                                                                                  |
| **Memory ordering**                    | Correct under the C11 acquire/release model on weakly-ordered CPUs; validated natively on AArch64. All atomics route through `seqlock_conf.h` macros (`__atomic`, C11, or uniprocessor fallback)                                                                                                                                                                                                |
| **Counter wrap**                       | A `uint32_t` counter cannot cause an accepted torn read: that would require completing a full `2^32` write cycle during one reader's copy. Widen via `SEQLOCK_COUNTER_TYPE` if desired                                                                                                                                                                                                          |
| **ThreadSanitizer**                    | The TSAN job auto-selects the race-free atomic copy and must run on an x86-64 (or 4K-page AArch64) host. TSAN cannot initialise on a 16K-page AArch64 kernel (47-bit VMA); use a native or ASAN/UBSan build there                                                                                                                                                                               |
| **Lock-free counter**                  | A `_Static_assert` rejects a counter type that is not lock-free on the target, so the primitive fails to compile rather than degrade silently                                                                                                                                                                                                                                                   |
| **Version header**                     | `seqlock_version.h` is generated into the build folder and is optional                                                                                                                                                                                                                                                                                                                          |
