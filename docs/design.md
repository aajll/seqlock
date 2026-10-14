# Design

This document is the design record for `seqlock`: the protocol, the memory ordering that makes it correct, what each backend guarantees, and how the library is verified. The API is described in [api.md](api.md).

## Goals

- Publish a value wider than one machine word from one writer to any number of readers, so that every reader gets the value of one complete store, never a mixture of two.
- Never make the writer wait for a reader, so a slow or stalled reader cannot delay the producer.
- No allocation, no system calls, no operating-system locks, and no global state.
- Keep the ordering logic in one compiled function per operation, behind a type-erased interface, so it can be reviewed once.

## Non-goals

- More than one writer per cell. Concurrent stores corrupt the counter; serialise writers outside the library or use another primitive.
- Payloads that need a deep copy. The payload is copied byte for byte.
- A history or queue of values. A reader sees only the latest complete store, and a reader that runs slower than the writer skips values.
- Fairness or a bounded wait for `load`. `try_load` is the bounded alternative.

## Protocol

Each cell holds a sequence counter and the payload. The counter is even while the payload is stable and odd while a store is in progress. It starts at zero.

The writer, with the counter at an even value `s`:

1. Stores `s + 1`, marking a store in progress.
2. Issues a release fence (F1).
3. Copies the new value into the payload.
4. Stores `s + 2` with release ordering, marking the store complete.

The reader:

1. Loads the counter with acquire ordering, as `s1`. If `s1` is odd, a store is in progress, so it retries.
2. Copies the payload out.
3. Issues an acquire fence (F2).
4. Loads the counter again, as `s2`. If `s2` equals `s1`, no store began during the copy, and it accepts the copy. Otherwise it retries.

`try_load` runs the same loop and counts each pass, whether it saw an odd counter or a changed one, against its bound.

## Memory ordering

The acquire load of `s1` reads the value written by step 4 of some store. That release store synchronises with the acquire load, so every payload byte of that store is visible to the reader's copy.

The second check rejects a copy that read any byte of a later store. Suppose the copy read a byte that a later store wrote in its step 3. That byte was written after F1, and read before F2, so F1 synchronises with F2. The later store's odd counter value therefore happens before the reader's load of `s2`, which reads that value or a newer one, and so differs from `s1`. A copy that saw any part of a newer value always fails the check.

F1 is a hardware fence on a weakly ordered CPU, not just a compiler barrier. Without it, the payload stores of step 3 could become visible to another core before the odd counter of step 1, and a reader could copy half a new value between two loads of the same even counter.

The argument treats the payload accesses as atomic, which they are with `SEQLOCK_ATOMIC_PAYLOAD_COPY` set to 1: each byte is copied with relaxed `__atomic` operations, the copy is free of data races in the C11 model, and the protocol matches the seqlock construction that Boehm describes in "Can Seqlocks Get Along With Programming Language Memory Models?" (MSPC 2012). With the default of 0, the copy is `memcpy`. A reader's copy that overlaps a store is then a data race, which the C11 standard leaves undefined. The library relies on GCC and Clang treating the fences and atomic counter accesses as compiler barriers, so that `memcpy` stays between them and compiles to ordinary loads and stores, whose result the counter check discards. See [payload copy](#payload-copy).

## Backends

`seqlock_conf.h` maps the counter accesses and fences onto one of three backends, chosen at compile time:

- **GCC and Clang `__atomic` builtins**, the default on those compilers. The counter is a plain integer accessed with `__atomic_load_n` and `__atomic_store_n`, and the fences are `__atomic_thread_fence`.
- **C11 `<stdatomic.h>`**, the default on other C11 compilers that do not define `__STDC_NO_ATOMICS__`. The counter is `_Atomic`, accessed with `atomic_load_explicit` and `atomic_store_explicit`, and the fences are `atomic_thread_fence`.
- **Uniprocessor**, the fallback for every other compiler. The counter is `volatile` and the fences do nothing. See [uniprocessor backend](#uniprocessor-backend).

The two atomic backends refuse to compile when the target cannot access the counter type lock-free, rather than fall back to a lock that a reader could hold while the writer waits. The `__atomic` backend checks `__atomic_always_lock_free`. The C11 backend matches the counter's size to `char`, `short`, `int`, `long` or `long long` and requires the matching `ATOMIC_*_LOCK_FREE` macro to be 2, so a counter that is only sometimes lock-free is rejected. Both backends also reject a signed counter type, whose overflow would be undefined.

### Uniprocessor backend

This backend is correct only when a reader and the writer never run at the same time on different cores: a single-core target, or threads pinned to one core. Without fences, nothing orders the payload stores against the counter for another core.

On one core, the writer and the readers interleave only through preemption, and the outcome depends on which preempts which:

- A reader that preempts the writer, such as an interrupt handler reading a cell that the main loop writes, can arrive with the counter odd. The writer cannot finish until the reader returns, so `load` never returns. Read with `try_load` in that context, or make the store run with that reader masked.
- A writer that preempts a reader, such as an interrupt handler writing a cell that the main loop reads, always completes its store before the reader resumes. The reader sees the changed counter and retries.

The backend has two further limits that the library cannot check:

- `volatile` orders the counter accesses with respect to each other, but the C standard does not stop the compiler moving the non-volatile payload copy across them. The backend relies on the compiler keeping the copy between the two counter stores in the writer and between the two counter loads in the reader. Inspect the generated code for the store and load functions when you adopt a new compiler release or enable whole-program optimisation.
- The atomic backends check that the counter is lock-free; this one does not check that the target reads and writes the counter in one access. A counter that the target accesses in two halves can be read half old and half new when an interrupt arrives between them. Use a counter type the target accesses with one instruction.

## Payload copy

The writer's copy-in and the reader's copy-out share one helper, chosen by `SEQLOCK_ATOMIC_PAYLOAD_COPY`:

- At 0, the default outside ThreadSanitizer, the helper calls `memcpy`. It is the fastest copy, and the only one available to compilers without `__atomic` builtins. The helper carries the `no_sanitize("thread")` attribute, so a ThreadSanitizer build that forces this copy does not report the race the counter check makes harmless.
- At 1, the default under ThreadSanitizer, the helper copies one byte at a time with relaxed atomic loads and stores. There is no data race for ThreadSanitizer to report, so a clean run does not depend on the sanitiser honouring an attribute through its `memcpy` interceptor, and every counter access stays instrumented.

The concurrency tests therefore run with the race-free copy under ThreadSanitizer and with `memcpy` everywhere else.

GCC reports `-Wtsan` for each standalone fence, because ThreadSanitizer does not model them. `src/seqlock.c` silences that warning only for GCC builds under ThreadSanitizer. The ordering ThreadSanitizer checks there comes from the acquire and release counter accesses, which it does model.

## Progress

The writer is wait-free. A store is two counter stores, a fence and a copy of fixed size, with no loop, so it finishes in a bounded number of steps whatever the readers do.

A reader is lock-free while the writer runs. A reader retries only when a store was in progress or began during its copy, so every retry corresponds to progress by the writer. A writer that stores continuously can make one reader retry many times, lowering its read rate; `SEQLOCK_CPU_RELAX()` runs on every retry to ease the contention, and on a core shared with the writer through simultaneous multithreading it yields the pipeline to the writer.

A reader is not wait-free, and not even lock-free when the writer stops mid-store. If the writer is suspended or killed between its two counter stores, the counter stays odd and `load` spins until the writer resumes. A writer suspended between stores leaves the counter even and does not affect readers. `try_load` bounds the work in every case.

## Counter wrap

The counter is unsigned and wraps through zero, so the parity test and the equality test stay correct across the wrap. Each store adds 2, so a reader could accept a mixed copy only if exactly 2^31 stores, with the default 32-bit counter, completed during one payload copy and brought the counter back to `s1`. A `uint64_t` counter raises that to 2^63 stores.

## Failure behaviour

The library has no error codes. Each function validates its pointers and size and degrades to a defined result: `init` and `store` do nothing, `load` zeroes the destination when it can, and `try_load` also returns `false`. [api.md](api.md#core-functions) lists the cases. A NULL cell is a caller defect, and zeroing the destination keeps a reader's result defined rather than stale.

Breaking the single-writer rule, or calling `init` while the cell is in use, is undefined behaviour that the library cannot detect.

## MISRA C:2023

`misch` analyses `include/` and `src/` once for each backend: the `__atomic` backend in the base configuration, and the C11 and uniprocessor backends through the `c11` and `no_atomics` profiles in `misra.toml`, because each backend compiles different code. Each analysis has a baseline in `analysis/baseline/` that records its accepted findings, and CI fails on any finding outside it. No deviation is recorded for any finding.

The baselines hold 42, 36 and 35 findings. All three share the same core of Rules 2.5, 8.7, 12.1, 15.5 and 20.10. The rest follow from the backend: Rule 17.3 only for the `__atomic` backend, whose builtins have no declaration; Rule 10.4 only for the C11 backend; and Rules 12.3 and 13.4 only for the uniprocessor backend, whose counter-store macro uses the comma operator and an assignment as an operand. Full tool-driven compliance needs a certified static analyser, which this repository does not vendor.

## Verification

CI runs every test under ASan and UBSan, and again under ThreadSanitizer, on x86-64 Linux. A third build gates on 100% line and branch coverage of `src/` and `include/`, merged across the backend builds. The tests are:

- **basic** and **core** — round trips through the typed and core interfaces, repeated stores, the zeroed value after `init`, the even counter between stores, a large payload, and every NULL and zero-size argument.
- **try_load** — success on a quiet cell, failure with a bound of 0, and failure when the counter is held odd.
- **wrap** — stores and loads across the counter's wrap from `UINT32_MAX` to 0.
- **c11** and **no_atomics** — the round trip with the library and the test both built for the C11 and uniprocessor backends.
- **retry** and **retry relax** — each reader retry branch reached in a single thread, by moving the counter during the copy and by holding it odd until a test hook runs in place of `SEQLOCK_CPU_RELAX()`.
- **mt** — 3 readers against a writer of 2,000,000 stores of a 128-byte payload: a seed and 15 words derived from it. Every accepted value is checked against its seed, so any mixed copy fails the test, and after the writer finishes the cell must hold its last value.
- **soak** — 4 readers against 5,000,000 stores of a 64-byte payload, a seed and 7 words, with the same kind of check.

Under a sanitiser, mt and soak run 100,000 and 300,000 stores, because a sanitiser slows them by an order of magnitude and finds races from interleavings, not volume.

Removing the reader's comparison of `s1` and `s2` makes both mt and soak fail with a mixed copy, so they detect the failure they exist for. On x86-64, whose stores are not reordered with other stores, they cannot detect a missing fence; the full suite and the ThreadSanitizer build also pass natively on an AArch64 host.

CI also compiles the library with the TI C2000 code generation tools 22.6.2 and 25.11.1, for both the COFF and EABI object formats, with warnings as errors. That compiler selects the uniprocessor backend. The check covers the source dialect only; the consumer owns linking, execution, and the [uniprocessor limits](#uniprocessor-backend) on its target.
