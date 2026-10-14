/*
 * SPDX-License-Identifier: MIT
 *
 * @file test_seqlock_backend.c
 * @brief Exercises a forced atomic backend.
 *
 * On GCC/Clang the default backend is the `__atomic` builtin path, so the C11
 * and no-atomics paths would otherwise never be compiled in CI.
 * tests/meson.build builds this executable once per non-default backend,
 * passing the backend define for both this TU and the core so the separately
 * compiled seqlock.c agrees on the backend and ABI. A failure here is most
 * likely a compile error: the C11 lock-free assertion rejecting the counter
 * type, or a backend's atomic macros not expanding cleanly.
 */

#include "seqlock.h"
#include "seqlock_test.h"

#include <stdint.h>

SEQLOCK_DEFINE(backend_cell, uint64_t)

TEST_CASE(test_backend_round_trip)
{
        backend_cell_t cell;
        uint64_t out = 0U;
        uint64_t in = 0x0123456789ABCDEFULL;

        backend_cell_init(&cell);
        backend_cell_load(&cell, &out);
        TEST_ASSERT(out == 0U);

        backend_cell_store(&cell, &in);
        backend_cell_load(&cell, &out);
        TEST_ASSERT(out == in);

        out = 0U;
        TEST_ASSERT(backend_cell_try_load(&cell, &out, 8U));
        TEST_ASSERT(out == in);
}

int
main(void)
{
        (void)fprintf(stdout, "\n=== seqlock forced-backend tests ===\n\n");

        run_test(test_backend_round_trip, "backend_round_trip");

        (void)fprintf(stdout, "\n=== all forced-backend tests passed ===\n\n");
        return EXIT_SUCCESS;
}
