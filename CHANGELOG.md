# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- `scripts/format.sh`, which pins the clang-format release and runs it with `uvx`. CI checks formatting through it.
- CI runs the MISRA C:2023 analysis with `misch` for each atomic backend, fails on any finding outside that backend's recorded baseline, and prints the deviation record.
- CI compiles the library with warnings as errors using two releases of a vendor C11 compiler for a 16-bit-MAU DSP target, under both of its object formats. That compiler selects the uniprocessor backend.

### Changed

- The build produces and installs only the static library. The shared library, which neither `seqlock_dep` nor `seqlock.pc` linked, is no longer built.
- CI installs Meson, Ninja and gcovr with `uv` instead of `pip`.
- CI gates on 100% branch coverage as well as line coverage. The reader retry branches are reached deterministically by a single-threaded test, and the no-atomics backend is built and tested alongside the C11 backend.

- Added security policy, SPDX headers, and cleaned ignore rules while leaving the existing special coverage/concurrency handling unchanged.

## [0.1.1] - 2026-06-03

### Fixed

- `seqlock_dep` now links the static archive via `seqlock_lib.get_static_lib()` so static subproject consumers no longer pull in a shared-library runtime dependency.

## [0.1.0] - 2026-05-27

### Added

- Initial sequence-lock primitive with typed macro API, type-erased core, bounded `try_load`, weak-memory-safe ordering, TSAN-aware payload copying, user-overridable configuration, compile-time validation, Meson packaging, CI, and concurrency tests.
