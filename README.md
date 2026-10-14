# seqlock

[![CI](https://github.com/aajll/seqlock/actions/workflows/ci.yml/badge.svg)](https://github.com/aajll/seqlock/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**Single-writer, multi-reader sequence lock for C11: one producer publishes a multi-word value and any number of readers take consistent snapshots of it without locks.**

## Quick start

Copy `include/` and `src/seqlock.c` into your project. For a Meson subproject or an installed `pkg-config` package, see the [integration guide](docs/integration.md).

```c
#include <stdbool.h>
#include <stdint.h>

typedef struct {
        float value;
        uint64_t timestamp_ns;
        bool valid;
} io_sample_t;

/* Generates the cell type io_cache_t and io_cache_init/store/load/try_load. */
SEQLOCK_DEFINE(io_cache, io_sample_t)

static io_cache_t cache;

/* Producer: the only writer of this cell. */
void
publish(float value, uint64_t timestamp_ns)
{
        io_sample_t s = {.value = value, .timestamp_ns = timestamp_ns,
                         .valid = true};
        io_cache_store(&cache, &s);
}

/* Consumers: any number, concurrently with the producer. */
bool
sample_is_fresh(uint64_t now_ns, uint64_t max_age_ns)
{
        io_sample_t s;
        io_cache_load(&cache, &s); /* never a mix of two stores */
        return s.valid && (now_ns - s.timestamp_ns) <= max_age_ns;
}

int
main(void)
{
        io_cache_init(&cache); /* before any store or load */
        publish(3.5f, 1000u);
        return sample_is_fresh(1500u, 1000u) ? 0 : 1; /* returns 0 */
}
```

## Description

A sequence lock pairs a value with a counter that is odd while a write is in progress. The writer never waits for readers, and a reader that overlaps a write detects it from the counter and retries, so it only ever returns a complete value.

- `SEQLOCK_DEFINE(name, type)` generates a typed cell that holds the payload by value, with no heap and no system calls
- Wait-free `store`, lock-free `load`, and a bounded `try_load` for callers that must not spin
- Correct on weakly ordered CPUs through acquire and release ordering, with GCC/Clang `__atomic`, C11 `<stdatomic.h>`, or a uniprocessor `volatile` backend selected at compile time
- The counter, fence and copy protocol is compiled once, in `src/seqlock.c`, behind a type-erased core
- Unit, concurrency and soak tests under ASan, UBSan and ThreadSanitizer with 100% line and branch coverage, and MISRA C:2023 analysis with [`misch`](https://pypi.org/project/misch/)

## Validated toolchains

| Toolchain               | Target                        | Validation                                                         |
| ----------------------- | ----------------------------- | ------------------------------------------------------------------ |
| GCC                     | x86_64 Linux                  | All tests under ASan and UBSan, and under ThreadSanitizer           |
| DSP vendor C11 compiler | 16-bit MAU (`CHAR_BIT == 16`) | Library compile with the uniprocessor backend, two object formats  |

Target linking and execution belong to the consuming firmware. See [cross builds](docs/integration.md#cross-builds).

## Documentation

- [Integration guide](docs/integration.md) — copy-in, Meson subproject, installed package, configuration, building, and cross builds
- [API reference](docs/api.md) — the typed cell, the core functions, configuration macros, and recipes
- [Design](docs/design.md) — the protocol, memory ordering, backends, progress guarantees, and verification
- [Changelog](CHANGELOG.md) — release history

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) before you open a pull request. Report security issues as described in [SECURITY.md](.github/SECURITY.md).

## License

MIT. See [LICENSE](LICENSE).
