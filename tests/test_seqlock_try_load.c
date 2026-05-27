/*
 * @file test_seqlock_try_load.c
 * @brief Bounded try_load() semantics.
 *
 * Deterministic, single-threaded: a quiescent cell succeeds; a zero bound makes
 * no attempt; a counter stuck odd (simulating a perpetual in-flight write)
 * exhausts the bound and returns false within it rather than spinning forever.
 */

#include "seqlock.h"
#include "seqlock_test.h"

#include <stdint.h>

typedef struct {
        uint32_t a;
        uint32_t b;
} pair_t;

SEQLOCK_DEFINE(pair_cell, pair_t)

TEST_CASE(test_try_load_quiescent_succeeds)
{
        pair_cell_t cell;
        pair_t in = {11U, 22U};
        pair_t out = {0U, 0U};

        pair_cell_init(&cell);
        pair_cell_store(&cell, &in);

        TEST_ASSERT(pair_cell_try_load(&cell, &out, 1U) == true);
        TEST_ASSERT(out.a == 11U && out.b == 22U);

        /* A generous bound also succeeds on the first usable attempt. */
        TEST_ASSERT(pair_cell_try_load(&cell, &out, 1000U) == true);
        TEST_ASSERT(out.a == 11U && out.b == 22U);
}

TEST_CASE(test_try_load_zero_bound_fails)
{
        pair_cell_t cell;
        pair_t in = {1U, 2U};
        pair_t out = {0U, 0U};

        pair_cell_init(&cell);
        pair_cell_store(&cell, &in);

        /* Zero iterations => no attempt => false. */
        TEST_ASSERT(pair_cell_try_load(&cell, &out, 0U) == false);
}

TEST_CASE(test_try_load_stuck_odd_exhausts)
{
        pair_cell_t cell;
        pair_t in = {5U, 6U};
        pair_t out = {0U, 0U};

        pair_cell_init(&cell);
        pair_cell_store(&cell, &in);

        /* Force the counter odd: every attempt sees "write in progress", so
         * try_load can never confirm a snapshot and must return false within
         * the bound (this call returning at all proves it does not hang). */
        cell.hdr.seq = (SEQLOCK_COUNTER_TYPE)1U;
        TEST_ASSERT(pair_cell_try_load(&cell, &out, 8U) == false);

        /* Restore an even counter and confirm it succeeds again. */
        cell.hdr.seq = (SEQLOCK_COUNTER_TYPE)2U;
        TEST_ASSERT(pair_cell_try_load(&cell, &out, 8U) == true);
        TEST_ASSERT(out.a == 5U && out.b == 6U);
}

int
main(void)
{
        (void)fprintf(stdout, "\n=== seqlock try_load tests ===\n\n");

        run_test(test_try_load_quiescent_succeeds, "try_load_quiescent");
        run_test(test_try_load_zero_bound_fails, "try_load_zero_bound");
        run_test(test_try_load_stuck_odd_exhausts, "try_load_stuck_odd");

        (void)fprintf(stdout, "\n=== all try_load tests passed ===\n\n");
        return EXIT_SUCCESS;
}
