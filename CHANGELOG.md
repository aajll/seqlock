# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Changed

- Added security policy, SPDX headers, and cleaned ignore rules while leaving the existing special coverage/concurrency handling unchanged.

## [0.1.1] - 2026-06-03

### Fixed

- `seqlock_dep` now links the static archive via `seqlock_lib.get_static_lib()` so static subproject consumers no longer pull in a shared-library runtime dependency.

## [0.1.0] - 2026-05-27

### Added

- Initial sequence-lock primitive with typed macro API, type-erased core, bounded `try_load`, weak-memory-safe ordering, TSAN-aware payload copying, user-overridable configuration, compile-time validation, Meson packaging, CI, and concurrency tests.
