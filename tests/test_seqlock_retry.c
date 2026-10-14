/*
 * SPDX-License-Identifier: MIT
 *
 * @file test_seqlock_retry.c
 * @brief Drives every reader retry branch deterministically.
 *
 * The concurrent tests reach the retry paths only when a writer happens to
 * interleave, which is not enough to gate branch coverage. This executable
 * reaches them on every run, single-threaded:
 *   - The output buffer aliases the counter, so the copy itself moves the
 *     counter between the reader's two loads.
 *   - With SEQLOCK_TEST_RELAX_HOOK, SEQLOCK_CPU_RELAX is replaced
 *     (tests/meson.build) with a hook that completes a pending write, so a
 *     blocking load that starts on an odd counter retries exactly once.
 *
 * tests/meson.build builds this file twice: against the library, and with the
 * hook. The hook adds a call to the core's control flow, so gcov numbers that
 * build's branches separately, and each build must reach its own.
 */

#include "seqlock.h"
#include "seqlock_test.h"

#include <stdbool.h>
#include <stdint.h>

static seqlock_hdr_t hdr;

#if defined(SEQLOCK_TEST_RELAX_HOOK)

static uint32_t relax_calls;
static bool complete_write;

void seqlock_test_relax(void);

void
seqlock_test_relax(void)
{
        ++relax_calls;
        if (complete_write) {
                hdr.seq = (SEQLOCK_COUNTER_TYPE)(hdr.seq + 1U);
        }
}

#define RELAX_CALLS_RESET()  (relax_calls = 0U)
#define RELAX_CALLS_EQUAL(n) (relax_calls == (n))

TEST_CASE(test_load_retries_while_odd)
{
        const uint64_t payload = 0x1122334455667788ULL;
        uint64_t out = 0U;

        hdr.seq = (SEQLOCK_COUNTER_TYPE)1U;
        RELAX_CALLS_RESET();
        complete_write = true;
        seqlock_core_load(&hdr, &payload, &out, sizeof out);
        complete_write = false;

        TEST_ASSERT(RELAX_CALLS_EQUAL(1U));
        TEST_ASSERT(hdr.seq == (SEQLOCK_COUNTER_TYPE)2U);
        TEST_ASSERT(out == payload);
}

#else

/* The default relax hint is not observable, so only outcomes are checked. */
#define RELAX_CALLS_RESET()  ((void)0)
#define RELAX_CALLS_EQUAL(n) ((void)(n), true)

#endif

TEST_CASE(test_load_retries_when_counter_moves)
{
        const SEQLOCK_COUNTER_TYPE next = (SEQLOCK_COUNTER_TYPE)4U;

        hdr.seq = (SEQLOCK_COUNTER_TYPE)2U;
        RELAX_CALLS_RESET();
        seqlock_core_load(&hdr, &next, (void *)&hdr.seq, sizeof hdr.seq);

        TEST_ASSERT(RELAX_CALLS_EQUAL(1U));
        TEST_ASSERT(hdr.seq == next);
}

TEST_CASE(test_try_load_retries_when_counter_moves)
{
        const SEQLOCK_COUNTER_TYPE next = (SEQLOCK_COUNTER_TYPE)4U;

        /* One attempt: the counter moves during the copy, so the bound is
         * exhausted. */
        hdr.seq = (SEQLOCK_COUNTER_TYPE)2U;
        RELAX_CALLS_RESET();
        TEST_ASSERT(!seqlock_core_try_load(&hdr, &next, (void *)&hdr.seq,
                                           sizeof hdr.seq, 1U));
        TEST_ASSERT(RELAX_CALLS_EQUAL(1U));

        /* Two attempts: the second sees a stable counter and succeeds. */
        hdr.seq = (SEQLOCK_COUNTER_TYPE)2U;
        RELAX_CALLS_RESET();
        TEST_ASSERT(seqlock_core_try_load(&hdr, &next, (void *)&hdr.seq,
                                          sizeof hdr.seq, 2U));
        TEST_ASSERT(RELAX_CALLS_EQUAL(1U));
        TEST_ASSERT(hdr.seq == next);
}

int
main(void)
{
        (void)fprintf(stdout, "\n=== seqlock reader retry tests ===\n\n");

#if defined(SEQLOCK_TEST_RELAX_HOOK)
        run_test(test_load_retries_while_odd, "load_retries_while_odd");
#endif
        run_test(test_load_retries_when_counter_moves,
                 "load_retries_when_counter_moves");
        run_test(test_try_load_retries_when_counter_moves,
                 "try_load_retries_when_counter_moves");

        (void)fprintf(stdout, "\n=== all reader retry tests passed ===\n\n");
        return EXIT_SUCCESS;
}
