# Contributing to seqlock

seqlock is a small C library providing a single-writer / multi-reader
sequence lock: a lock-free way to publish a multi-word value as an
internally consistent snapshot. It is designed to be safe to drop into
embedded firmware and audited, safety-adjacent environments.

## Getting started

The same commands CI runs, locally:

```sh
# Configure with tests + sanitisers (CI default)
meson setup build --buildtype=debug -Dbuild_tests=true \
                  -Db_sanitize=address,undefined
meson compile -C build
meson test -C build --verbose

# ThreadSanitizer (separate from ASan; CI runs a dedicated job)
meson setup build-tsan --buildtype=debug -Dbuild_tests=true \
                       -Db_sanitize=thread
meson compile -C build-tsan
meson test -C build-tsan --verbose

# Coverage (CI gates on 100% line coverage of src/ + include/)
meson setup build-cov --buildtype=debug -Dbuild_tests=true -Db_coverage=true \
                      -Dc_args=-fprofile-update=atomic
meson compile -C build-cov && meson test -C build-cov
gcovr --root . --filter 'src/' --filter 'include/' \
      --fail-under-line 100 --print-summary
```

Run the TSAN build on an x86-64 host (or a 4K-page AArch64). TSAN cannot
initialise on a 16K-page AArch64 kernel (47-bit VMA); use a native or
ASan/UBSan build to validate the weak-memory path there.

`-fprofile-update=atomic` is required for the coverage build. gcov's profile
counters are not thread-safe by default, and the concurrency tests update the
same instrumented lines from multiple threads. Without atomic counters those
updates race, the arc counts become inconsistent, and gcov's flow-graph solver
derives negative branch counts that corrupt the report (and abort gcovr). The
atomic-update flag costs a little runtime but is the correct fix; do not paper
over the symptom with `--gcov-ignore-parse-errors`.

## Source style

- `.clang-format` is mandatory. Run `clang-format -i` on every modified
  `.c` / `.h` file before submitting.
- 8-space indent, Linux brace style, 80-column limit. Match the existing
  conventions; do not reformat unrelated code.
- Avoid em dashes in comments, doc-comments, and prose.
- The Meson build system is the single source of truth. Update
  `meson.build` / `tests/meson.build` when adding or removing source files.
- No CMake, no Make, no other build systems.

## C language rules

- C11 only (uses `_Static_assert`, `_Atomic`, `_Alignof`, `_Alignas`).
- Use fixed-width types from `<stdint.h>` and `<stdbool.h>`. Never plain
  `int` for fixed-width fields.
- No heap allocation (`malloc`, `free`, VLAs), no syscalls, no blocking on
  any path.
- Validate pointer arguments at every public-API boundary.
- All public identifiers use the `seqlock_` / `SEQLOCK_` prefix.
- Public functions return `void` or `bool` and validate via early return. No
  `errno`, no exceptions.
- All atomics and fences go through the `seqlock_conf.h` macros so the
  backend stays swappable; do not call `__atomic_*` or `<stdatomic.h>`
  directly from the core.

## Concurrency rules

The correctness of this library lives in its memory ordering. Keep these
invariants:

- The writer's release fence after the odd increment (F1) is **mandatory**
  on weakly-ordered CPUs. A plain compiler barrier is **not** sufficient; do
  not "simplify" it away.
- The even increment publishes via a release store; the reader's first load
  is acquire and its second load is preceded by an acquire fence. Do not
  weaken these orderings.
- `store()` is single-producer; concurrent writers are undefined behaviour.
- The payload-copy race is intentional and benign. If you change the copy,
  the TSAN concurrency test must stay clean **and** the deterministic
  torn-read test must still pass.
- Any change to the protocol or payload copy must be validated under
  ThreadSanitizer (race detection) and natively on AArch64 (real
  weak-memory ordering).

## MISRA C:2012

The library is written against MISRA C:2012. The full deviation record lives
in the file header of `src/seqlock.c`. If your change introduces a new
deviation:

1. Add an entry to the file-header deviation record (rule, site,
   justification).
2. Add an inline `/* MISRA <rule> */` marker at the deviation site.
3. Justify the deviation in the PR description.

`required` rule deviations face a higher bar than `advisory`. We currently
have only advisory deviations and want to keep it that way.

## Tests and coverage

- Add a test for every bug fix and every new feature.
- Tests live in `tests/test_*.c` using the built-in harness in
  `tests/seqlock_test.h` (`TEST_CASE` / `TEST_ASSERT` / `run_test`). Do not
  add Unity or other external test dependencies.
- The concurrent torn-read test (`tests/test_seqlock_mt.c`) doubles as the
  TSAN gate. Its payload carries a self-checking invariant so a torn read is
  caught at runtime, independent of the sanitiser. Keep new concurrency tests
  deterministic in their pass/fail criterion (assert an invariant, not a
  timing).
- Concurrency tests shrink their iteration budget under a sanitiser
  (`SEQLOCK_TEST_SANITIZED` in `tests/seqlock_test.h`): sanitisers add a
  10-50x slowdown and race detection comes from interleavings, not volume.
  Keep the full budget for native builds. Counts are `-D`-overridable.
- The type-erased core (`seqlock_core_*`) is exercised directly by
  `tests/test_seqlock_core.c`, including its argument-validation paths, which
  the typed shims cannot reach. New core behaviour needs a test there.
- CI gates on **100% line coverage** of `src/` and `include/`. Branch coverage
  is reported but not gated, because some branches are concurrency-dependent
  and cannot be forced deterministically every run. New code without tests
  fails the gate.
- All tests must pass on every CI configuration: Linux + macOS with
  ASan/UBSan, the dedicated TSAN job, the coverage gate, and the release
  build.

## API stability

- The public API in `seqlock.h` and the configuration surface in
  `seqlock_conf.h` are stable across a minor (`0.x` / `x.y`) line.
- The generated cell layout `{ seqlock_hdr_t hdr; type payload; }` is part of
  the contract; changing member order or names is a breaking change.
- Breaking changes go in a new major release and are recorded in
  `CHANGELOG.md`.

## Commits

Use Conventional Commits:

- `feat: ...` new feature
- `fix: ...` bug fix
- `doc: ...` documentation only
- `test: ...` test-only changes
- `chore: ...` build, CI, release work
- `refactor: ...` code change that neither fixes a bug nor adds a feature

Keep the subject under ~70 characters. Use the body to explain _why_ the
change is needed, not _what_ the diff already shows.

## Pull requests

- Open an issue first for non-trivial changes, especially anything touching
  the memory-ordering protocol, so the design can be agreed before
  implementation.
- Keep PRs focused. One feature or one fix per PR.
- All CI checks must pass: tests on Linux + macOS, ASan/UBSan, the TSAN job,
  and the release build.
- Flag any new MISRA deviation in the PR description.

## When in doubt

Open an issue and discuss before writing code. The library is small and its
correctness is subtle; even modest changes to the protocol have outsized
implications.
