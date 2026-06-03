# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.1] - 2026-06-03

### Fixed

- `seqlock_dep` now links the static archive via
  `seqlock_lib.get_static_lib()` instead of the `both_libraries` object,
  which resolved to the shared library. Consumers vendoring seqlock as a
  static subproject no longer pull in a `libseqlock.so` runtime dependency,
  matching the static archive already advertised by the generated
  `pkg-config` file

## [0.1.0] - 2026-05-27

Initial release.

### Added

- Single-writer / multi-reader sequence lock with lock-free reads and a
  wait-free writer, publishing a multi-word value as an internally consistent
  snapshot (no torn tuples)
- `SEQLOCK_DEFINE(name, type)` typed-macro API generating a cell type and its
  `init` / `store` / `load` / `try_load` operations, storing the payload by
  value with no dynamic allocation
- Type-erased core (`seqlock_core_init` / `_store` / `_load` / `_try_load`)
  compiled once in `seqlock.c`, with the typed operations as thin inline shims
- Bounded `try_load(max_retries)` variant that gives up after a caller-set
  number of attempts instead of spinning
- C11 memory-ordering implementation correct on weakly-ordered CPUs, routed
  through `seqlock_conf.h` macros with a GCC/Clang `__atomic` path, a C11
  `<stdatomic.h>` path, and a uniprocessor fallback
- ThreadSanitizer-clean concurrency handling: a race-free relaxed-atomic
  payload copy auto-selected under TSAN, and a fast benign-race-annotated
  `memcpy` otherwise (`SEQLOCK_ATOMIC_PAYLOAD_COPY`)
- User-overridable configuration via `seqlock_conf.h`: `SEQLOCK_COUNTER_TYPE`,
  `SEQLOCK_DEFAULT_MAX_RETRIES`, `SEQLOCK_CELL_ALIGNED`, `SEQLOCK_CACHELINE`,
  and the `SEQLOCK_CPU_RELAX()` reader-retry spin hint (`pause` on x86,
  `yield` on AArch64)
- Compile-time validation: `_Static_assert` rejects a non-unsigned or
  non-lock-free counter type (on both the `__atomic` and C11 backends), a
  zero-size payload, and an over-aligned payload
- Meson build as `both_libraries` with version/soversion, `pkg-config`
  generation, an optional generated `seqlock_version.h`, and subproject support
- CI (GitHub Actions) for Ubuntu and macOS with address and
  undefined-behaviour sanitisers, a dedicated ThreadSanitizer job, and a
  release build
- Unit test suite covering single-threaded round-trip, pointer validation,
  the even-counter invariant, bounded `try_load`, counter wrap, the C11
  `<stdatomic.h>` backend, a concurrent self-checking torn-read test (also the
  TSAN gate), and a multi-reader soak; validated natively on AArch64

[0.1.1]: https://github.com/aajll/seqlock/releases/tag/v0.1.1
[0.1.0]: https://github.com/aajll/seqlock/releases/tag/v0.1.0
