# Integration guide

This guide shows how to add `seqlock` to a project, configure it, build it, and run its tests. The API is described in [api.md](api.md), and the protocol and its guarantees in [design.md](design.md).

## Requirements

- A C11 compiler; the headers use `_Static_assert`, `_Alignof` and, when cache-line padding is on, `_Alignas`.
- One of the three atomic backends: GCC or Clang `__atomic` builtins, C11 `<stdatomic.h>`, or a single core on which readers and the writer never run in parallel. The header selects one automatically; [design.md](design.md#backends) sets out what each guarantees.
- A sequence counter type that the target reads and writes lock-free. The two atomic backends check this at compile time.
- For the tests: Meson, Ninja, Python 3, and POSIX threads.

## Copy-in

Copy three files into your source tree:

```
include/seqlock.h
include/seqlock_conf.h
src/seqlock.c
```

Keep both headers in the same directory, because `seqlock.h` includes `seqlock_conf.h`. Then include the public header:

```c
#include "seqlock.h"
```

`seqlock_version.h` exists only in Meson builds.

## Meson subproject

Put the repository in `subprojects/seqlock`, as a checkout or through a wrap file, then declare the dependency:

```meson
seqlock_dep = dependency('seqlock', fallback: ['seqlock', 'seqlock_dep'])
```

The build calls `meson.override_dependency('seqlock', ...)`, so other subprojects in the same build find `seqlock` by name. `seqlock_dep` links the static library. Include the header as `"seqlock.h"`. Tests are off unless the `build_tests` option is set.

| Option | Default | Effect |
| --- | --- | --- |
| `build_tests` | `false` | Builds the unit, backend, concurrency and soak tests, and the documentation example test. |

## Installed package

```sh
meson setup build --buildtype=release -Dbuild_tests=false --prefix=/usr/local
meson compile -C build
meson install -C build
cc app.c $(pkg-config --cflags --libs seqlock) -o app
```

The install contains `libseqlock.a`, the headers in a `seqlock/` subdirectory, and `seqlock.pc`. The `pkg-config` flags add that directory to the include path, so the same include works as in a copy-in build. Without `pkg-config`, include the namespaced path instead:

<!-- snippet: skip -->
```c
#include <seqlock/seqlock.h>
#include <seqlock/seqlock_version.h>
```

`seqlock_version.h` defines `SEQLOCK_VERSION_STRING` as the version string, and `SEQLOCK_VERSION_MAJOR`, `SEQLOCK_VERSION_MINOR` and `SEQLOCK_VERSION_PATCH` as integers.

## Configuration

Every option in `seqlock_conf.h` sits behind an `#ifndef` guard, so a `-D` definition replaces the shipped default. The options are described in [api.md](api.md#configuration).

`SEQLOCK_COUNTER_TYPE` and the backend selection macros change the layout and access width of `seqlock_hdr_t`, which the library and its callers share. Define them identically for `src/seqlock.c` and for every file that includes `seqlock.h`. Nothing detects a mismatch: a caller built with a 64-bit counter against a library built with a 32-bit one compiles, links, and reads half a counter. `SEQLOCK_CELL_ALIGNED` and `SEQLOCK_CACHELINE` change the layout of the generated cell, so every file that shares a cell needs the same values.

In a Meson build, set them in the top-level project's `c_args`, which also apply to subprojects:

```sh
meson setup build "-Dc_args=-DSEQLOCK_COUNTER_TYPE=uint64_t"
```

A per-target `c_args` or an `add_project_arguments` call in the parent project does not reach the subproject, and an installed library keeps the values it was built with.

`seqlock.h` includes `seqlock_conf.h` with quotes, so the compiler finds the copy beside `seqlock.h` before any directory on the include path. A project cannot replace it by putting its own `seqlock_conf.h` earlier on the include path; use `-D` definitions instead.

## Build and test

```sh
# Library only
meson setup build --buildtype=release -Dbuild_tests=false
meson compile -C build

# All tests under ASan and UBSan
meson setup build --buildtype=debug -Dbuild_tests=true -Db_sanitize=address,undefined
meson compile -C build
meson test -C build --verbose

# The same tests under ThreadSanitizer
meson setup build-tsan --buildtype=debug -Dbuild_tests=true -Db_sanitize=thread
meson compile -C build-tsan
meson test -C build-tsan --verbose
```

ThreadSanitizer and AddressSanitizer each need a large virtual address space. They run on x86-64 and on AArch64 kernels with 4 KiB pages and a 48-bit address space. ThreadSanitizer cannot start on a 16 KiB-page AArch64 kernel, and AddressSanitizer cannot start on a kernel with a 39-bit address space; run a plain native build there to exercise the weak-memory path.

Coverage, formatting and MISRA commands are in [CONTRIBUTING.md](../CONTRIBUTING.md).

## Cross builds

The consuming firmware owns its cross file, linker command file, and memory placement. No cross file ships with this project.

CI compiles the library with warnings as errors using two releases of a vendor C11 compiler for a 16-bit-MAU DSP target, under both of its object formats. That compiler provides neither GCC builtins nor `<stdatomic.h>`, so it selects the uniprocessor backend. This covers the source dialect and type assumptions only; linking and execution remain checks for the consumer. Read the [uniprocessor backend](design.md#uniprocessor-backend) limits before you use it.
