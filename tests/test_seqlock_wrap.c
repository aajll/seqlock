/*
 * @file test_seqlock_wrap.c
 * @brief Counter wrap across the counter type maximum.
 *
 * The counter member is part of the generated cell's public layout, so the
 * test presets it near its maximum (an even value) and drives stores across
 * the wrap, asserting the even/odd invariant and consistent reads hold. Parity
 * survives the wrap because 2^N is even.
 */

#include "seqlock.h"
#include "seqlock_test.h"

#include <stdint.h>

typedef struct {
        uint32_t tag;
        uint32_t mirror; /* == ~tag; a torn read would break this */
} wrap_t;

SEQLOCK_DEFINE(wrap_cell, wrap_t)

TEST_CASE(test_counter_wrap)
{
        wrap_cell_t cell;
        wrap_t out;
        uint32_t i;
        const SEQLOCK_COUNTER_TYPE max =
            (SEQLOCK_COUNTER_TYPE) ~(SEQLOCK_COUNTER_TYPE)0U;

        wrap_cell_init(&cell);

        /* Preset to max-1, which is even (max == all-ones is odd). */
        cell.hdr.seq = (SEQLOCK_COUNTER_TYPE)(max - 1U);
        TEST_ASSERT((cell.hdr.seq & 1U) == 0U);

        /* Drive several stores so the counter crosses max -> 0. */
        for (i = 1U; i <= 8U; ++i) {
                wrap_t in = {i, ~i};
                wrap_cell_store(&cell, &in);

                /* Always even after a completed store, including across wrap.
                 */
                TEST_ASSERT((cell.hdr.seq & 1U) == 0U);

                wrap_cell_load(&cell, &out);
                TEST_ASSERT(out.tag == i);
                TEST_ASSERT(out.mirror == ~i);
        }

        /* The first store from max-1 must have wrapped the counter to 0. */
        /* (max-1) + 2*8 = max-1+16 wraps; just confirm it is small, not huge.
         */
        TEST_ASSERT(cell.hdr.seq < 32U);
}

int
main(void)
{
        (void)fprintf(stdout, "\n=== seqlock wrap test ===\n\n");

        run_test(test_counter_wrap, "counter_wrap");

        (void)fprintf(stdout, "\n=== wrap test passed ===\n\n");
        return EXIT_SUCCESS;
}
