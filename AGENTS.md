# AGENTS.md

## Project-specific instructions

**Project:** `seqlock`
**Primary goal:** Single-writer / multi-reader sequence lock (seqlock) for lock-free, internally consistent snapshots of a multi-word value. Readers never block the writer and never observe a torn tuple. Targets IEC 61508 and MISRA C:2012 principles.

### Essential commands

#### Configure and build (library only)

```sh
meson setup build --wipe --buildtype=release -Dbuild_tests=false
meson compile -C build
```

#### Configure, build, and run unit tests

```sh
meson setup build --wipe --buildtype=debug -Dbuild_tests=true
meson compile -C build
meson test -C build --verbose
```

#### Concurrency check (ThreadSanitizer)

```sh
meson setup build-tsan --wipe --buildtype=debug -Dbuild_tests=true -Db_sanitize=thread
meson test -C build-tsan --verbose
```

Run TSAN on an x86-64 host (or a 4K-page AArch64). TSAN cannot initialise on a
16K-page AArch64 kernel (47-bit VMA); use a native or ASAN/UBSan build there.

### CI / source of truth

- CI definitions live in `.github/workflows/ci.yml`.
- A dedicated `tsan` job runs the thread sanitizer; ASAN and TSAN are mutually
  exclusive, so it is separate from the ASAN/UBSan matrix.
- Prefer running the same commands locally as CI runs.
- If `pre-commit` is configured later, run it before committing.

## Docs / commit conventions

- Use Conventional Commits when asked to commit.
- Keep commits focused and explain why the change exists.
- **Never** commit unless the user explicitly asks.

## C style expectations

### Build and configuration

- Use the Meson build system; do not introduce another build system.
- Update `meson.build` when adding or removing source files or public headers.
- `seqlock_conf.h` is a public configuration header; install it alongside
  `seqlock.h`.
- The library is `both_libraries('seqlock', 'src/seqlock.c', ...)`: the compiled
  core is a real translation unit, not an empty stub.

### Formatting

- `.clang-format` is present and should be used on modified `.c` and `.h` files.
- Do not reformat unrelated code.
- Key settings: 8-space indent, `BreakBeforeBraces: Linux`, column limit 80.
- Avoid em dashes in comments, doc-comments, and prose.

### Style and correctness

- Match the conventions in the existing files.
- All public identifiers use the `seqlock_` / `SEQLOCK_` prefix.
- Keep public headers minimal and stable; do not expose internal helpers.
- Prefer explicit fixed-width integer types when ABI or serialization matters.
- No dynamic memory, no syscalls, no blocking anywhere in the library.
- Compile-time validation via `_Static_assert` (lock-free counter, non-empty and
  non-over-aligned payload); the primitive must fail to compile rather than
  degrade silently.
- Public symbols carry Doxygen annotations with full contract information
  (preconditions, concurrency, return semantics).
- Use `seqlock_conf.h` for compile-time configuration options. It is included
  automatically by `seqlock.h` and every option is `#ifndef`-guarded so a
  consumer can override it (e.g. via a project `config/seqlock_conf.h`).
- **Do not** preserve backwards compatibility unless the user explicitly asks.

### Architecture / API design

- Hybrid layout: a type-erased core (`seqlock_core_*` in `src/seqlock.c`) holds
  all correctness-critical logic, and `SEQLOCK_DEFINE(name, type)` emits thin
  `static inline` typed shims that forward to it. Keep the protocol, fences, and
  TSAN handling in the core, in one place; the shims only add type safety.
- The generated cell layout is fixed and part of the contract:
  `{ seqlock_hdr_t hdr; type payload; }`, `hdr` first.
- Single-producer: at most one thread calls `store()` per cell; concurrent
  writers are undefined behaviour.
- `load()` / `try_load()` are safe for any number of readers concurrent with the
  writer. The payload must be trivially copyable POD, stored by value.
- `init()` is setup-only and must not run concurrently with `store()`/`load()`.
- `try_load()` returns `false` on bound exhaustion; `*out` is then unspecified
  (no scratch buffer is allocated). Callers branch on the return value.

### Concurrency correctness (read before touching the protocol)

- The writer's release fence after the odd increment (F1) is **mandatory** on
  weakly-ordered CPUs; a plain compiler barrier is NOT sufficient. Do not
  "simplify" it away.
- The even increment publishes via a release store; the reader's first load is
  acquire and its second load is preceded by an acquire fence. Do not weaken
  these orderings.
- Payload-copy race handling uses a benign `no_sanitize("thread")` `memcpy`
  normally, and a race-free relaxed-atomic copy under TSAN (auto-selected via
  `SEQLOCK_ATOMIC_PAYLOAD_COPY`). If you change the copy, the TSAN test must
  stay clean and the deterministic torn-read test must still pass.
- All atomics and fences go through the `seqlock_conf.h` macros so the backend
  (`__atomic`, C11, or uniprocessor fallback) stays swappable.

### Testing

- Run `meson test -C build` after changes; also run the TSAN build for any
  change to the protocol or the payload copy.
- Tests live in `tests/test_*.c` using the built-in harness in
  `tests/seqlock_test.h` (`TEST_CASE` / `TEST_ASSERT` / `run_test`). Do not add
  Unity or other external test dependencies.
- The multi-threaded torn-read test (`tests/test_seqlock_mt.c`) doubles as the
  TSAN gate; its payload carries a self-checking invariant so any torn read is
  detected at runtime, independent of the sanitizer.
- Add a test case for each bug fix and each new feature.
