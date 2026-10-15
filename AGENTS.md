# AGENTS.md

---

## 1) Project-specific instructions

**Project:** `seqlock`
**Primary goal:** A C11 single-writer, multi-reader sequence lock: one producer publishes a multi-word value with a wait-free store, any number of readers take consistent copies with a lock-free load, and no reader ever accepts a mixture of two stores. No allocation, no system calls, no locks.

### 1.1 Essential commands

#### Configure and build (library only)

```sh
meson setup build --wipe --buildtype=release -Dbuild_tests=false
meson compile -C build
```

#### Configure, build, and run unit tests

```sh
meson setup build --wipe --buildtype=debug -Dbuild_tests=true -Db_sanitize=address,undefined
meson compile -C build
meson test -C build --verbose
```

#### Concurrency check (ThreadSanitizer)

```sh
meson setup build-tsan --wipe --buildtype=debug -Dbuild_tests=true -Db_sanitize=thread
meson test -C build-tsan --verbose
```

#### MISRA C:2023, once per atomic backend (reads `build/compile_commands.json`)

```sh
env -u MISRA_RULE_TEXTS misch run --baseline
env -u MISRA_RULE_TEXTS misch run --baseline --profile c11
env -u MISRA_RULE_TEXTS misch run --baseline --profile no_atomics
misch deviations
```

#### Formatting (pinned clang-format through `uvx`)

```sh
scripts/format.sh          # check, as CI does
scripts/format.sh --fix    # rewrite in place
```

### 1.2 Notes

- `include/seqlock.h` defines the version macros, and `meson setup` fails when they differ from `project(version:)`, so a release updates both. `include/seqlock_version.h` is a deprecated header that includes `seqlock.h`.
- The build produces only a static library. The only Meson option is `build_tests`.
- `SEQLOCK_COUNTER_TYPE` and the backend macros must be identical for the library and every caller; nothing detects a mismatch. `seqlock.h` includes `seqlock_conf.h` with quotes, so configuration is by `-D` only, never by a replacement header.
- ThreadSanitizer needs a 4 KiB-page kernel with a 48-bit address space on AArch64, and AddressSanitizer does not start on a 39-bit one; use a plain native build there.

---

## 2) Non-negotiable design invariants

- **Protocol.** Odd counter store, release fence (F1), payload copy, release store of the next even value. Reader: acquire load, copy, acquire fence, relaxed load, accept only on equality. Never remove F1 or weaken an ordering; a compiler barrier is not a substitute on weakly ordered CPUs.
- **One writer per cell.** `store` is never concurrent with another `store` on the same cell, and `init` is never concurrent with anything.
- **Type-erased core.** The protocol, fences and payload copy live once, in `src/seqlock.c`. `SEQLOCK_DEFINE` emits only `static inline` forwarders, and the cell layout `{ seqlock_hdr_t hdr; type payload; }` is part of the contract.
- **Selectable backend.** Every counter access and fence goes through the `seqlock_conf.h` macros. The two atomic backends must reject a counter that is not always lock-free at compile time.
- **Payload copy.** `memcpy` under `no_sanitize("thread")` by default, relaxed byte-wise atomics under ThreadSanitizer. A change to the copy keeps ThreadSanitizer clean and the mt and soak tests passing.
- No heap, no system calls, no blocking, no global state.

---

## 3) CI / source of truth

- CI definitions live in `.github/workflows/ci.yml`.
- Prefer running the same commands locally as CI runs (see §1.1 above).
- Install Python tools (Meson, Ninja, gcovr, misch) with `uv tool install`, never `pip`; CI does the same through `astral-sh/setup-uv`. CI installs `cppcheck` from Homebrew because Ubuntu's 2.13 reports different findings.
- `misch run --baseline` must pass for the base configuration and both profiles; each has its own baseline in `analysis/baseline/`. Never regenerate a baseline to admit a new finding. Run it with `MISRA_RULE_TEXTS` unset, as CI does.
- The CI jobs are: tests under ASan/UBSan, the same tests under ThreadSanitizer, a release build, a 100 percent line and branch coverage gate merged across backends, a TI C2000 library build (CGT 22.6.2 and 25.11.1, COFF and EABI, warnings as errors), `clang-format`, and `misch` for each backend.

---

## 4) Docs / commit conventions

### Documentation layout

- `README.md` is a short landing page (about 85 lines). Put detail in `docs/`, never in the README; link to the owning file instead.
- `docs/integration.md` owns requirements, install methods, configuration consistency, building, sanitiser host limits, and cross builds.
- `docs/api.md` owns the API reference, the configuration macros and recipes. Change it together with the headers; keep signatures in fenced `c` blocks and describe them in prose, not function tables. Check every value an example states against a build.
- `docs/design.md` is the single authoritative design record: protocol, memory ordering, backends and their limits, payload copy, progress, counter wrap, failure behaviour, MISRA position, and verification.
- `CONTRIBUTING.md` documents the contributor workflow; `CHANGELOG.md` holds release history (Keep a Changelog); `.github/SECURITY.md` covers vulnerability reporting.
- Code blocks in `README.md`, `CONTRIBUTING.md`, and `docs/` are compiled by the `seqlock docs examples` test (`tests/docs/check_snippets.py`). Each document is compiled as one source file: a `c` fence is file-level code, `<!-- snippet: body -->` wraps statements, and `<!-- snippet: skip -->` excludes pseudo-code. Do not use `skip` to silence a broken example.
- Prose uses British spelling. Do not hard-wrap Markdown: write one line per paragraph. The 80-column limit applies to C source, not Markdown.

### Commits

- Use **Conventional Commits** format when asked to commit.
- Keep commits focused; explain *why* in the message body.

---

## 5) C style expectations

### Build & configuration

- Use the Meson build system. Do not introduce CMake, Make, or other systems.
- Update `meson.build` and `tests/meson.build` when adding or removing files.
- `seqlock_conf.h` is public and installed beside `seqlock.h`. Every option in it stays behind an `#ifndef` guard.

### Formatting

- `.clang-format` is present and **mandatory**. Run `scripts/format.sh --fix` before committing. It runs the clang-format release pinned in the script through `uvx`. Do not use a system `clang-format`: another release can format the same code differently.
- Do not reformat unrelated code.
- Key settings: 8-space indent, `BreakBeforeBraces: Linux`, column limit 80.
- Keep each file's existing `/* ==== NAME ==== */` section banners.

### Style & correctness

- Match conventions in the existing files (indentation, braces, naming).
- Every public identifier uses the `seqlock_` or `SEQLOCK_` prefix. Keep public headers minimal; do not expose internal helpers.
- Validate pointer arguments at every public API boundary. Public functions return `void` or `bool`; no `errno`.
- Use fixed-width types from `<stdint.h>`; never plain `int` for a fixed-width quantity.
- Reject an unusable configuration with `_Static_assert` or `#error` rather than degrading at run time.
- Public symbols carry Doxygen with the full contract: preconditions, concurrency, and return semantics.

### MISRA deviations

- Prefer compliant code; never restructure only to hide a finding. Deviate at the point of use with `/* cppcheck-suppress misra-c2012-<rule> ; @deviation <rationale> */`, or project-wide in `analysis/deviations/misra-deviations.txt` for a house-style rule.

### Comment placement (Doxygen)

- Inline trailing annotations (`/**< ... */`) on `enum`/`struct` members are allowed only when the resulting line fits the 80-column limit.
- If any member's annotation would overrun, move **all** of that aggregate's member docs into a single structured Doxygen block above the type, as an `@details` list of `- ::SYMBOL  description` entries.
- Never mix inline and block forms within one aggregate, and never leave a trailing comment that clang-format would wrap onto a second line.
- After editing, run `scripts/format.sh` and an 80-column scan.

### Testing

- Run `meson test -C build` after every change, and the ThreadSanitizer build for any change to the protocol, a backend or the payload copy.
- Tests use the harness in `tests/seqlock_test.h` (`TEST_CASE`, `TEST_ASSERT`, `run_test`). Do not add Unity or another external framework.
- Concurrency tests assert an invariant, never a timing. Reach new branches deterministically in a single thread, as `tests/test_seqlock_retry.c` does.
- Add a test case for each bug fix and each new feature.
- Keep `src/` and `include/` at the coverage gate (100 percent line and branch).
