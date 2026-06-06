/**
 * SPDX-License-Identifier: MIT
 *
 * @file: seqlock.h
 *
 * @brief
 *    Single-writer / multi-reader sequence lock (seqlock) primitive.
 *
 * @details
 *    A seqlock publishes a multi-word value as an internally consistent
 *    snapshot for a lock-free, read-mostly workload: one producer stores the
 *    latest value (wait-free), any number of consumers load it (lock-free),
 *    and a reader never blocks the writer nor observes a torn tuple.
 *
 *    This header exposes two layers:
 *      1. A type-erased core (`seqlock_core_*`, compiled in seqlock.c) that
 *         performs the counter/fence/copy protocol on `(hdr, payload, size)`.
 *      2. `SEQLOCK_DEFINE(name, type)` which generates a typed cell plus thin
 *         `static inline` shims that forward to the core with `sizeof(type)`.
 *
 * @note  Single-producer only: concurrent `store()` on one cell is undefined
 *        behaviour. Payload must be trivially copyable POD. See the design
 *        document for the full contract.
 */

#ifndef SEQLOCK_H_
#define SEQLOCK_H_

/* ================ INCLUDES ================================================ */

#include "seqlock_conf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ================ COMPILE-TIME GUARANTEES ================================= */

/* The protocol relies on unsigned wraparound and parity (even/odd); a signed
 * counter would be undefined behaviour on overflow. */
_Static_assert(
    (SEQLOCK_COUNTER_TYPE)-1 > (SEQLOCK_COUNTER_TYPE)0,
    "seqlock: SEQLOCK_COUNTER_TYPE must be an unsigned integer type");

#if defined(SEQLOCK_USE_GNU_ATOMICS)
/* Degrade loudly, not silently, if the counter would need a lock on this
 * target (e.g. a 64-bit counter on a 32-bit ISA without 64-bit atomics).
 * __atomic_always_lock_free is a compile-time constant but not a "standard"
 * integer constant expression, so -Wpedantic objects to it inside a
 * _Static_assert; suppress just that diagnostic here. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
_Static_assert(__atomic_always_lock_free(sizeof(SEQLOCK_COUNTER_TYPE), 0),
               "seqlock: SEQLOCK_COUNTER_TYPE is not lock-free on this target");
#pragma GCC diagnostic pop
#elif defined(SEQLOCK_USE_C11_ATOMICS)
/* The C11 path has no size-based lock-free query (atomic_is_lock_free is a
 * runtime call), so match the counter width to the matching ATOMIC_*_LOCK_FREE
 * macro and require "always lock-free" (== 2). Every term is an integer
 * constant expression, so this is a valid static assertion. Conservative by
 * design: a "sometimes lock-free" (== 1) counter is rejected. */
_Static_assert(
    (sizeof(SEQLOCK_COUNTER_TYPE) == 1 && ATOMIC_CHAR_LOCK_FREE == 2)
        || (sizeof(SEQLOCK_COUNTER_TYPE) == sizeof(short)
            && ATOMIC_SHORT_LOCK_FREE == 2)
        || (sizeof(SEQLOCK_COUNTER_TYPE) == sizeof(int)
            && ATOMIC_INT_LOCK_FREE == 2)
        || (sizeof(SEQLOCK_COUNTER_TYPE) == sizeof(long)
            && ATOMIC_LONG_LOCK_FREE == 2)
        || (sizeof(SEQLOCK_COUNTER_TYPE) == sizeof(long long)
            && ATOMIC_LLONG_LOCK_FREE == 2),
    "seqlock: SEQLOCK_COUNTER_TYPE is not always-lock-free on this target");
#endif

/* ================ TYPES =================================================== */

/**
 * @brief Sequence-counter header embedded as the first member of every cell.
 *
 * Even = stable, odd = a write is in progress. The counter is the only
 * field touched atomically; the payload that follows it in the cell is copied
 * under the protocol implemented by the core.
 */
typedef struct {
        SEQLOCK_ATOMIC_QUAL SEQLOCK_COUNTER_TYPE seq;
} seqlock_hdr_t;

/* ================ TYPE-ERASED CORE API ==================================== */

/**
 * @brief Initialise a cell to a stable, zeroed state.
 *
 * Sets the counter to an even value (stable) and zeroes @p size bytes of
 * @p payload, so a reader racing a never-yet-written cell observes a defined
 * all-zero snapshot rather than indeterminate memory.
 *
 * @param hdr      Cell header (sequence counter). No-op if NULL.
 * @param payload  Payload storage. Zeroed when non-NULL and @p size != 0.
 * @param size     Payload size in bytes.
 *
 * @pre   Setup-only. Must NOT run concurrently with seqlock_core_store() or
 *        seqlock_core_load() on the same cell; establish the cell before any
 *        reader or the writer touches it.
 * @note  Idempotent. There is no destroy/reset counterpart: the cell owns no
 *        resources, so re-initialising is simply another call under this same
 *        no-concurrency rule.
 */
void seqlock_core_init(seqlock_hdr_t *hdr, void *payload, size_t size);

/**
 * @brief Publish a new snapshot. Sole producer, wait-free.
 *
 * Copies @p size bytes from @p in into the cell under the sequence-counter
 * protocol, so a concurrent reader observes either the complete previous
 * snapshot or the complete new one, never a torn mixture.
 *
 * @param hdr      Cell header. No-op if NULL.
 * @param payload  Cell payload storage. No-op if NULL.
 * @param in       Source value to copy in. No-op if NULL.
 * @param size     Payload size in bytes. No-op if 0.
 *
 * @pre   At most one thread ever calls store() on a given cell; concurrent
 *        writers are undefined behaviour.
 * @pre   The payload type is trivially copyable POD (no pointers needing a
 *        deep copy, no self-references).
 * @note  Wait-free: the critical section is a bounded copy with no syscalls,
 *        no allocation, and no spinning, so it completes in bounded time
 *        regardless of reader behaviour.
 */
void seqlock_core_store(seqlock_hdr_t *hdr, void *payload, const void *in,
                        size_t size);

/**
 * @brief Read the current consistent snapshot. Any number of readers,
 *        lock-free. No failure return for valid arguments.
 *
 * Retries internally while a write overlaps the read, returning only once it
 * has copied an internally consistent (possibly slightly stale) snapshot. If
 * the counter remains odd, this call spins until the writer completes; use
 * seqlock_core_try_load() when a hard retry bound is required.
 *
 * @param hdr      Cell header. Invalid if NULL.
 * @param payload  Cell payload storage. Invalid if NULL.
 * @param out      Destination for the snapshot. Invalid if NULL. If an
 *                 invalid argument is detected and @p out is non-NULL with
 *                 @p size != 0, @p out is zeroed before returning.
 * @param size     Payload size in bytes. Invalid if 0.
 *
 * @note  Safe for any number of concurrent readers and concurrent with the
 *        single writer when arguments are valid. It never blocks the writer,
 *        allocates, performs syscalls, or takes an OS lock, but a reader may
 *        spin while the writer is in the odd-counter window.
 */
void seqlock_core_load(const seqlock_hdr_t *hdr, const void *payload, void *out,
                       size_t size);

/**
 * @brief Bounded read: like seqlock_core_load() but gives up after a fixed
 *        number of attempts instead of retrying until success.
 *
 * @param hdr          Cell header. Invalid if NULL.
 * @param payload      Cell payload storage. Invalid if NULL.
 * @param out          Destination for the snapshot on success. Invalid if
 *                     NULL. If an invalid argument is detected and @p out is
 *                     non-NULL with @p size != 0, @p out is zeroed before
 *                     returning false.
 * @param size         Payload size in bytes. Invalid if 0.
 * @param max_retries  Maximum loop iterations attempted. Each iteration counts
 *                     as one attempt, whether it observes a write in progress
 *                     (odd counter) or observes the counter change across the
 *                     copy.
 *
 * @return true with @p out filled on success; false on invalid arguments or if
 *         the bound was exhausted without a consistent snapshot.
 *
 * @note  On retry-bound exhaustion, the contents of @p out are unspecified: a
 *        partial attempt may have been written, and the no-allocation rule
 *        precludes a scratch buffer. Branch on the return value, never on
 *        @p out.
 * @note  Same reader concurrency guarantees as seqlock_core_load().
 */
bool seqlock_core_try_load(const seqlock_hdr_t *hdr, const void *payload,
                           void *out, size_t size, uint32_t max_retries);

/* ================ TYPED MACRO ============================================= */

#if defined(__GNUC__) || defined(__clang__)
#define SEQLOCK__UNUSED __attribute__((unused))
#else
#define SEQLOCK__UNUSED
#endif

#if SEQLOCK_CELL_ALIGNED
#define SEQLOCK_CELL_ALIGNAS _Alignas(SEQLOCK_CACHELINE)
#else
#define SEQLOCK_CELL_ALIGNAS
#endif

/**
 * @brief Generate a typed cell type and its typed seqlock operations.
 *
 * Defines `name##_t` (a cell holding one @p type payload by value) and four
 * `static inline` operations that forward to the type-erased core with
 * `sizeof(type)`:
 *
 * @code
 *     void name##_init (name##_t *cell);
 *     void name##_store(name##_t *cell, const type *in);     // sole producer
 *     void name##_load (const name##_t *cell, type *out);    // any readers
 *     bool name##_try_load(const name##_t *cell, type *out,
 *                          uint32_t max_retries);
 * @endcode
 *
 * Each operation carries the contract of the core function it forwards to
 * (seqlock_core_init / _store / _load / _try_load). In particular: `store` is
 * single-producer (concurrent writers are undefined behaviour), `load` and
 * `try_load` are safe for any number of concurrent readers, and @p type must
 * be trivially copyable POD. Every operation validates its pointers and is a
 * safe no-op (or zeroes @p out) on NULL.
 *
 * @param name  Identifier prefix; generates `name##_t` and `name##_*` ops.
 * @param type  Trivially copyable POD payload type, stored by value.
 *
 * The generated cell layout is fixed and part of the contract: the members
 * `hdr` and `payload` are referenced by the shims and may be accessed directly
 * (e.g. by tests presetting the counter):
 * @code
 *     typedef struct { seqlock_hdr_t hdr; type payload; } name##_t;
 * @endcode
 *
 * Compile-time checks reject a zero-size payload and an over-aligned payload.
 * Use without a trailing semicolon, e.g. `SEQLOCK_DEFINE(io_cell, io_t)`.
 *
 * @note Under GCC/Clang's zero-size-struct extension a type genuinely can be
 *       sizeof == 0, so the static assert against the sizeof the type is valid
 *       and just a defensive precaution. However, when consumed your lsp might
 *       give you a warning since in standard C sizeof(anything) ≥ 1.
 */
#define SEQLOCK_DEFINE(name, type)                                             \
        _Static_assert(sizeof(type) > 0,                                       \
                       #name ": seqlock payload type must be non-empty");      \
        _Static_assert(_Alignof(type) <= _Alignof(max_align_t),                \
                       #name ": seqlock payload type is over-aligned");        \
        typedef struct {                                                       \
                SEQLOCK_CELL_ALIGNAS seqlock_hdr_t hdr;                        \
                type payload;                                                  \
        } name##_t;                                                            \
                                                                               \
        static inline SEQLOCK__UNUSED void name##_init(name##_t *cell)         \
        {                                                                      \
                seqlock_core_init((cell != NULL) ? &cell->hdr : NULL,          \
                                  (cell != NULL) ? &cell->payload : NULL,      \
                                  sizeof(type));                               \
        }                                                                      \
                                                                               \
        static inline SEQLOCK__UNUSED void name##_store(name##_t *cell,        \
                                                        const type *in)        \
        {                                                                      \
                seqlock_core_store((cell != NULL) ? &cell->hdr : NULL,         \
                                   (cell != NULL) ? &cell->payload : NULL, in, \
                                   sizeof(type));                              \
        }                                                                      \
                                                                               \
        static inline SEQLOCK__UNUSED void name##_load(const name##_t *cell,   \
                                                       type *out)              \
        {                                                                      \
                seqlock_core_load((cell != NULL) ? &cell->hdr : NULL,          \
                                  (cell != NULL) ? &cell->payload : NULL, out, \
                                  sizeof(type));                               \
        }                                                                      \
                                                                               \
        static inline SEQLOCK__UNUSED bool name##_try_load(                    \
            const name##_t *cell, type *out, uint32_t max_retries)             \
        {                                                                      \
                return seqlock_core_try_load(                                  \
                    (cell != NULL) ? &cell->hdr : NULL,                        \
                    (cell != NULL) ? &cell->payload : NULL, out, sizeof(type), \
                    max_retries);                                              \
        }

#endif /* SEQLOCK_H_ */
