/*
 * @file test_seqlock_c11.c
 * @brief Exercises the C11 `<stdatomic.h>` atomic backend.
 *
 * On GCC/Clang the default backend is the `__atomic` builtin path, so the C11
 * path (and its lock-free static assertion) would otherwise never be compiled
 * in CI. This executable forces SEQLOCK_USE_C11_ATOMICS for both this TU and
 * the core (the define is passed on the command line in tests/meson.build, so
 * the separately compiled seqlock.c agrees on the backend and ABI). A failure
 * here is most likely a compile error: the C11 lock-free assertion rejecting
 * the counter type, or the C11 atomic macros not expanding cleanly.
 */

#include "seqlock.h"
#include "seqlock_test.h"

#include <stdint.h>

SEQLOCK_DEFINE(c11_cell, uint64_t)

TEST_CASE(test_c11_round_trip)
{
        c11_cell_t cell;
        uint64_t out = 0U;
        uint64_t in = 0x0123456789ABCDEFULL;

        c11_cell_init(&cell);
        c11_cell_load(&cell, &out);
        TEST_ASSERT(out == 0U);

        c11_cell_store(&cell, &in);
        c11_cell_load(&cell, &out);
        TEST_ASSERT(out == in);

        out = 0U;
        TEST_ASSERT(c11_cell_try_load(&cell, &out, 8U));
        TEST_ASSERT(out == in);
}

int
main(void)
{
        (void)fprintf(stdout,
                      "\n=== seqlock C11-atomics backend tests ===\n\n");

        run_test(test_c11_round_trip, "c11_round_trip");

        (void)fprintf(stdout,
                      "\n=== all C11-atomics backend tests passed ===\n\n");
        return EXIT_SUCCESS;
}
