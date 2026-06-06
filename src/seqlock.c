/**
 * SPDX-License-Identifier: MIT
 *
 * @file: seqlock.c
 *
 * @brief
 *    Type-erased core of the seqlock primitive.
 *
 * @details
 *    All correctness-critical logic lives here, once, operating on raw
 *    `(hdr, payload, size)`: the sequence-counter protocol, the memory fences,
 *    and the payload copy with its ThreadSanitizer handling. The typed
 *    `SEQLOCK_DEFINE` shims in seqlock.h are thin forwarders to these
 *    functions.
 *
 * @par MISRA C:2012 deviation record
 *    The library is written against MISRA C:2012 principles. Known deviations,
 *    all advisory and justified:
 *      - Compiler atomic builtins (`__atomic_*`, `__atomic_thread_fence`) are
 *        language extensions (Dir 1.1 / Rule 1.2). Justified: portable
 *        lock-free atomics; isolated behind `seqlock_conf.h` macros and
 *        swappable for the C11 `<stdatomic.h>` path.
 *      - `seqlock_core_load()` uses an unbounded `for (;;)` retry loop
 *        (Rule 14.x). Justified: a lock-free read must retry until it observes
 *        a consistent snapshot; `seqlock_core_try_load()` is provided for
 *        contexts that require a guaranteed upper bound.
 *      - The default payload copy is an intentional benign data race annotated
 *        `no_sanitize("thread")`. A fully race-free relaxed-atomic copy is
 *        auto-selected under ThreadSanitizer.
 *      - `#pragma GCC diagnostic` (Rule 20.x) silences one benign,
 *        sanitizer-only diagnostic at a single site.
 *      - The comma operator (Rule 12.3) appears only in the degenerate
 *        no-atomics (uniprocessor) fallback macros in `seqlock_conf.h`.
 */

/* ================ INCLUDES ================================================ */

#include "seqlock.h"

#include <string.h>

/*
 * GCC's ThreadSanitizer cannot model a standalone atomic_thread_fence and emits
 * an informational -Wtsan for each. It is benign here: under TSAN the payload
 * copy is race-free relaxed atomics and the publish/observe edge is carried by
 * the acquire/release counter load/store (both of which TSAN does model); the
 * fences remain for correctness on weakly-ordered hardware. Silence the note
 * on GCC-under-TSAN only (the option does not exist elsewhere).
 */
#if defined(__SANITIZE_THREAD__) && defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wtsan"
#endif

/* ================ PAYLOAD COPY ============================================ */

#if SEQLOCK_ATOMIC_PAYLOAD_COPY

/**
 * @brief Race-free payload copy: byte-wise relaxed atomics.
 *
 * Used when ThreadSanitizer is active (or forced via config). There is no data
 * race in the C11 sense (every access is atomic), so TSAN is clean with no
 * annotation. The counter's acquire/release still provides all ordering;
 * `relaxed` on the payload bytes is sufficient. Only reachable on GCC/Clang
 * (the only toolchains that define a thread sanitizer).
 */
static void
seqlock__copy(void *dst, const void *src, size_t size)
{
        unsigned char *d = (unsigned char *)dst;
        const unsigned char *s = (const unsigned char *)src;
        size_t i;

        for (i = 0U; i < size; ++i) {
                unsigned char b = __atomic_load_n(&s[i], __ATOMIC_RELAXED);
                __atomic_store_n(&d[i], b, __ATOMIC_RELAXED);
        }
}

#else

/**
 * @brief Benign-by-construction payload copy.
 *
 * The seq-counter protocol guarantees any torn copy is detected and discarded
 * by the reader, so the data a reader actually *uses* is never torn. The race
 * is suppressed only on this one helper; the counter accesses stay fully
 * instrumented. Used for both the writer's copy-in and the reader's copy-out,
 * so a single annotation covers both sites.
 */
SEQLOCK_NO_SANITIZE_THREAD
static void
seqlock__copy(void *dst, const void *src, size_t size)
{
        (void)memcpy(dst, src, size);
}

#endif /* SEQLOCK_ATOMIC_PAYLOAD_COPY */

/* ================ GLOBAL FUNCTIONS ======================================== */

void
seqlock_core_init(seqlock_hdr_t *hdr, void *payload, size_t size)
{
        if (hdr == NULL) {
                return;
        }

        /* Even counter == stable. No concurrency at init time, so a relaxed
         * store of the zeroed counter is sufficient. */
        SEQLOCK_ATOMIC_STORE(&hdr->seq, (SEQLOCK_COUNTER_TYPE)0,
                             SEQLOCK_MO_RELAXED);

        if (payload != NULL && size != 0U) {
                (void)memset(payload, 0, size);
        }
}

void
seqlock_core_store(seqlock_hdr_t *hdr, void *payload, const void *in,
                   size_t size)
{
        SEQLOCK_COUNTER_TYPE s;

        if (hdr == NULL || payload == NULL || in == NULL || size == 0U) {
                return;
        }

        s = SEQLOCK_ATOMIC_LOAD(&hdr->seq, SEQLOCK_MO_RELAXED);

        /* even -> odd: "write in progress". */
        SEQLOCK_ATOMIC_STORE(&hdr->seq, (SEQLOCK_COUNTER_TYPE)(s + 1U),
                             SEQLOCK_MO_RELAXED);

        /* (F1) Store-store barrier: the odd marker must become visible before
         * any payload store, else a reader could observe (even, half-written)
         * and accept a torn snapshot. A plain compiler barrier is NOT enough on
         * weakly-ordered CPUs. */
        SEQLOCK_FENCE_RELEASE();

        seqlock__copy(payload, in, size);

        /* odd -> even: "write complete". The release store publishes the
         * payload to any reader whose first load acquires this value. */
        SEQLOCK_ATOMIC_STORE(&hdr->seq, (SEQLOCK_COUNTER_TYPE)(s + 2U),
                             SEQLOCK_MO_RELEASE);
}

void
seqlock_core_load(const seqlock_hdr_t *hdr, const void *payload, void *out,
                  size_t size)
{
        SEQLOCK_COUNTER_TYPE s1;
        SEQLOCK_COUNTER_TYPE s2;

        if (hdr == NULL || payload == NULL || out == NULL || size == 0U) {
                if (out != NULL && size != 0U) {
                        (void)memset(out, 0, size);
                }
                return;
        }

        for (;;) {
                /* Acquire: synchronises-with the writer's release store and
                 * keeps the payload reads from hoisting above this load. */
                s1 = SEQLOCK_ATOMIC_LOAD(&hdr->seq, SEQLOCK_MO_ACQUIRE);
                if ((s1 & (SEQLOCK_COUNTER_TYPE)1U) != 0U) {
                        SEQLOCK_CPU_RELAX();
                        continue; /* write mid-flight; retry */
                }

                seqlock__copy(out, payload, size);

                /* Acquire fence: orders the payload reads before the second
                 * counter load. */
                SEQLOCK_FENCE_ACQUIRE();
                s2 = SEQLOCK_ATOMIC_LOAD(&hdr->seq, SEQLOCK_MO_RELAXED);

                if (s1 == s2) {
                        return; /* consistent snapshot */
                }
                SEQLOCK_CPU_RELAX(); /* counter moved during the copy; retry */
        }
}

bool
seqlock_core_try_load(const seqlock_hdr_t *hdr, const void *payload, void *out,
                      size_t size, uint32_t max_retries)
{
        SEQLOCK_COUNTER_TYPE s1;
        SEQLOCK_COUNTER_TYPE s2;
        uint32_t attempt;

        if (hdr == NULL || payload == NULL || out == NULL || size == 0U) {
                if (out != NULL && size != 0U) {
                        (void)memset(out, 0, size);
                }
                return false;
        }

        for (attempt = 0U; attempt < max_retries; ++attempt) {
                s1 = SEQLOCK_ATOMIC_LOAD(&hdr->seq, SEQLOCK_MO_ACQUIRE);
                if ((s1 & (SEQLOCK_COUNTER_TYPE)1U) == 0U) {
                        seqlock__copy(out, payload, size);
                        SEQLOCK_FENCE_ACQUIRE();
                        s2 = SEQLOCK_ATOMIC_LOAD(&hdr->seq, SEQLOCK_MO_RELAXED);
                        if (s1 == s2) {
                                return true;
                        }
                }
                SEQLOCK_CPU_RELAX(); /* retry pending; ease contention */
        }

        /* Bound exhausted: *out is unspecified; the caller branches on this
         * return value, never on *out. */
        return false;
}
