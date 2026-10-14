# Contributing to seqlock

`seqlock` is a small C11 sequence lock: one writer publishes a multi-word value and any number of readers take consistent copies of it without locks. Its correctness lives in a few memory-ordering choices that no single-threaded test can see, so read [design.md](docs/design.md) before you change the protocol, the fences, or the payload copy.

## Getting started

Install a C11 compiler and install the Python tools with [uv](https://docs.astral.sh/uv/), as CI does. The MISRA analysis also needs a current `cppcheck`; CI installs it from Homebrew because the 2.13 release in Ubuntu 24.04 reports different findings. The same commands CI runs, locally:

```sh
# Python tools
uv tool install meson
uv tool install ninja
uv tool install gcovr
uv tool install misch

# Configure with tests and sanitisers (CI default)
meson setup build --buildtype=debug -Dbuild_tests=true -Db_sanitize=address,undefined
meson compile -C build
meson test -C build --verbose

# ThreadSanitizer, which cannot share a build with AddressSanitizer
meson setup build-tsan --buildtype=debug -Dbuild_tests=true -Db_sanitize=thread
meson compile -C build-tsan
meson test -C build-tsan --verbose

# Release build (the configuration a downstream consumer uses)
meson setup build-rel --buildtype=release -Dbuild_tests=false
meson compile -C build-rel

# Coverage (CI gate is 100% line and branch for src/ and include/)
meson setup build-cov --buildtype=debug -Dbuild_tests=true -Db_coverage=true -Dc_args=-fprofile-update=atomic
meson compile -C build-cov && meson test -C build-cov
gcovr --root . --filter 'src/' --filter 'include/' --fail-under-line 100 --fail-under-branch 100 --print-summary

# MISRA C:2023, once per atomic backend (reads build/compile_commands.json)
env -u MISRA_RULE_TEXTS misch run --baseline
env -u MISRA_RULE_TEXTS misch run --baseline --profile c11
env -u MISRA_RULE_TEXTS misch run --baseline --profile no_atomics
misch deviations

# Formatting (pinned clang-format through uvx)
scripts/format.sh --fix
```

The coverage build needs `-fprofile-update=atomic`. The concurrency tests update the same instrumented lines from several threads, and gcov's default counters are not thread-safe, so without it the arc counts race and gcov derives negative branch counts that abort `gcovr`. Do not hide that with `--gcov-ignore-parse-errors`.

ThreadSanitizer and AddressSanitizer do not start on every AArch64 kernel; [integration.md](docs/integration.md#build-and-test) lists the limits. On such a host, run a plain native build of the full suite instead.

## Source style

- Formatting is mandatory. Run `scripts/format.sh --fix` before submitting, or `scripts/format.sh` to check without writing, as CI does. The script pins the clang-format release and runs it through `uvx`, so an editor formatter gives the same result only when it runs the same release.
- 8-space indent, Linux brace style, 80-column limit. Match the existing conventions, including each file's `/* ==== NAME ==== */` section banners, and do not reformat unrelated code.
- The Meson build system is the single source of truth. Update `meson.build` and `tests/meson.build` when you add or remove files. No CMake, no Make, and no other build systems.

## C language rules

- C11 only.
- Use fixed-width types from `<stdint.h>` and `<stdbool.h>`, and never plain `int` for a fixed-width quantity.
- No heap allocation (`malloc`, `free`, VLAs), no system calls, and no blocking on any path.
- Validate every pointer argument at the public API boundary. Public functions return `void` or `bool`; there is no `errno` and no exceptions.
- Every public identifier uses the `seqlock_` or `SEQLOCK_` prefix.
- Every atomic access and fence goes through the `seqlock_conf.h` macros, so that the backend stays selectable. Do not call `__atomic_*` or `<stdatomic.h>` directly from the core; the race-free payload copy, which exists only on GCC and Clang, is the one exception.
- Reject an unusable configuration at compile time with `_Static_assert` or `#error`, rather than degrading at run time.

## Concurrency rules

- The writer's release fence after the odd counter store (F1) is required on weakly ordered CPUs. A compiler barrier is not enough; do not remove it.
- The completing counter store is a release store. The reader's first counter load is an acquire load, and its second is preceded by an acquire fence. Do not weaken any of them.
- `store` has one writer per cell; concurrent writers are undefined behaviour.
- The default payload copy races with the writer by design, and the counter check discards any copy the race affects. If you change the copy, ThreadSanitizer must stay clean and the mt and soak tests must still pass.
- Validate any change to the protocol or the payload copy under ThreadSanitizer, and natively on an AArch64 host, where stores can be reordered.

## MISRA C:2023

The library is analysed with `misch` (cppcheck-backed MISRA C:2023 analysis), configured by `misra.toml`, over `include/` and `src/`. Each backend compiles different code, so the analysis runs three times: the `__atomic` backend in the base configuration, and the `c11` and `no_atomics` profiles. Each has its own baseline in `analysis/baseline/`, and CI fails on any finding that is not in the matching baseline. Run it from a configured `build/` directory, with `MISRA_RULE_TEXTS` unset, as CI does. If your change introduces a finding:

1. Prefer restructuring the code so the rule is satisfied. Deviate only when compliance would make the code genuinely worse, and never restructure purely to hide a finding from the checker; an honest deviation beats evasion.
2. Suppress at the point of use with `/* cppcheck-suppress misra-c2012-<rule> ; @deviation <rationale> */`, or add a justified project-wide entry to `analysis/deviations/misra-deviations.txt` for a house-style rule.
3. Verify with all three `misch run` commands and `misch deviations`, which requires every suppression to carry a rationale, and justify the deviation in the pull request description.

Do not regenerate a baseline to admit a new finding. A baseline changes only after a deliberate review, with `misch baseline` and the same `--profile`.

Full tool-driven compliance beyond this analysis needs a certified static analyser, which this repository does not vendor.

## Tests and coverage

- Add a test for every bug fix and every new feature.
- Tests live in `tests/test_*.c`, written with the `TEST_CASE`, `TEST_ASSERT` and `run_test` macros from `tests/seqlock_test.h`. Do not add Unity or another external test framework. Each new file is listed in `tests/meson.build`.
- Concurrency tests assert an invariant, never a timing. The mt and soak payloads derive every word from one seed, so a mixed copy fails at run time whether or not a sanitiser is present.
- Concurrency tests run a smaller iteration count under a sanitiser, through `SEQLOCK_TEST_SANITIZED` in `tests/seqlock_test.h`: a sanitiser slows them by an order of magnitude, and it finds races from interleavings, not volume. The counts can be overridden with `-D`.
- `tests/test_seqlock_core.c` calls the core functions directly, including the argument checks the typed functions cannot reach. New core behaviour needs a test there.
- CI enforces 100 percent line and branch coverage of `src/` and `include/`, merged across every backend build. Reach new branches deterministically, not through the concurrent tests; `tests/test_seqlock_retry.c` shows how the reader retry paths are driven in a single thread.
- The `seqlock docs examples` test compiles every C code block in `README.md`, `CONTRIBUTING.md`, and `docs/` against the public headers, through `tests/docs/check_snippets.py`. A block compiles at file scope by default. On the line directly above the fence, put `<!-- snippet: body -->` for a block of statements, or `<!-- snippet: skip -->` for pseudo-code or a copy of a header definition.

## Documentation

| File | Purpose |
| --- | --- |
| `README.md` | Landing page: quick start, description, validated toolchains, and links. Keep it short. |
| `docs/integration.md` | Requirements, installation, configuration consistency, building, and cross builds. |
| `docs/api.md` | Reference for the typed cell, the core functions, the configuration macros, and recipes. |
| `docs/design.md` | The protocol, memory ordering, backends, payload copy, progress, counter wrap, MISRA position, and verification. |

Update `docs/api.md` and the header Doxygen together when an interface changes, and check every value an example states against a build. Update `docs/design.md` when the protocol, a backend, or the verification changes. Keep it standalone: describe how the library works, not how it was built or which other projects use it. Write prose in British English, one line per paragraph.

## API stability

- The public API in `seqlock.h` and the configuration macros in `seqlock_conf.h` are stable within a minor release line.
- The generated cell layout, `seqlock_hdr_t hdr` followed by `type payload`, is part of the contract; changing the member order or names is a breaking change.
- Changing a function signature, the meaning of a configuration macro, or a default that changes the layout of `seqlock_hdr_t` is a breaking change and needs a major release.

## Commits

Use Conventional Commits:

- `feat: ...` new feature
- `fix: ...` bug fix
- `docs: ...` documentation only
- `test: ...` test-only changes
- `chore: ...` build, CI, release work
- `refactor: ...` a change that neither fixes a bug nor adds a feature

Keep the subject under about 70 characters. Use the body to explain why the change is needed.

## Pull requests

- Open an issue first for non-trivial changes, and always for a change to the memory-ordering protocol, so the design can be agreed before implementation.
- Keep pull requests focused: one feature or one fix.
- All CI checks must pass.
- Add a `CHANGELOG.md` entry under `## [Unreleased]` for a user-visible change.
- Flag any new MISRA deviation, and any change to the protocol or the payload copy, in the description.

## When in doubt

Open an issue. The library is small, but a modest change to its ordering can break it on hardware that no CI runner uses.
