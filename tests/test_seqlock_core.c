/*
 * @file test_seqlock_core.c
 * @brief Direct tests of the type-erased core API (`seqlock_core_*`).
 *
 * The typed `SEQLOCK_DEFINE` shims always pass non-NULL pointers and a non-zero
 * `sizeof(type)`, so they can never reach the core's argument-validation
 * branches. This exercises the core directly, both as the documented
 * runtime-sizing escape hatch and to cover those guards. The
 * counter header and payload are passed as separate pointers, mirroring how the
 * generated cell lays them out.
 */

#include "seqlock.h"
#include "seqlock_test.h"

#include <stdint.h>
#include <string.h>

#define WORDS 2U

TEST_CASE(test_core_round_trip)
{
        seqlock_hdr_t hdr;
        uint64_t payload[WORDS];
        uint64_t in[WORDS] = {0xDEADBEEFULL, 0x0123456789ABCDEFULL};
        uint64_t out[WORDS];

        seqlock_core_init(&hdr, payload, sizeof(payload));
        TEST_ASSERT((hdr.seq & 1U) == 0U);

        /* init zeroes the payload, so a load before any store reads zeros. */
        (void)memset(out, 0xAA, sizeof(out));
        seqlock_core_load(&hdr, payload, out, sizeof(payload));
        TEST_ASSERT(out[0] == 0U && out[1] == 0U);

        seqlock_core_store(&hdr, payload, in, sizeof(payload));
        seqlock_core_load(&hdr, payload, out, sizeof(payload));
        TEST_ASSERT(out[0] == in[0] && out[1] == in[1]);
        TEST_ASSERT((hdr.seq & 1U) == 0U);

        TEST_ASSERT(
            seqlock_core_try_load(&hdr, payload, out, sizeof(payload), 4U));
        TEST_ASSERT(out[0] == in[0] && out[1] == in[1]);
}

TEST_CASE(test_core_init_arg_validation)
{
        seqlock_hdr_t hdr;
        uint64_t payload[WORDS];

        /* NULL hdr: no-op, must not crash. */
        seqlock_core_init(NULL, payload, sizeof(payload));

        /* NULL payload / zero size: counter still initialised, no payload
         * touched (exercises the payload!=NULL and size!=0 guard branches). */
        hdr.seq = (SEQLOCK_COUNTER_TYPE)7U;
        seqlock_core_init(&hdr, NULL, sizeof(payload));
        TEST_ASSERT(hdr.seq == 0U);

        hdr.seq = (SEQLOCK_COUNTER_TYPE)9U;
        seqlock_core_init(&hdr, payload, 0U);
        TEST_ASSERT(hdr.seq == 0U);
}

TEST_CASE(test_core_store_arg_validation)
{
        seqlock_hdr_t hdr;
        uint64_t payload[WORDS] = {1U, 2U};
        uint64_t in[WORDS] = {9U, 9U};

        seqlock_core_init(&hdr, payload, sizeof(payload));

        /* Each invalid-argument form is a no-op: the counter never advances and
         * the payload is untouched. Covers every operand of the guard. */
        seqlock_core_store(NULL, payload, in, sizeof(payload));
        seqlock_core_store(&hdr, NULL, in, sizeof(payload));
        seqlock_core_store(&hdr, payload, NULL, sizeof(payload));
        seqlock_core_store(&hdr, payload, in, 0U);
        TEST_ASSERT(hdr.seq == 0U);
}

TEST_CASE(test_core_load_arg_validation)
{
        seqlock_hdr_t hdr;
        uint64_t payload[WORDS] = {5U, 6U};
        uint64_t out[WORDS];

        seqlock_core_init(&hdr, payload, sizeof(payload));

        /* NULL hdr with a real out buffer: out is zeroed, never indeterminate.
         */
        (void)memset(out, 0x5A, sizeof(out));
        seqlock_core_load(NULL, payload, out, sizeof(payload));
        TEST_ASSERT(out[0] == 0U && out[1] == 0U);

        /* Other invalid forms: must not crash. out==NULL exercises the inner
         * guard; size==0 exercises the size!=0 branch. */
        seqlock_core_load(&hdr, NULL, out, sizeof(payload));
        seqlock_core_load(&hdr, payload, NULL, sizeof(payload));
        seqlock_core_load(&hdr, payload, out, 0U);
}

TEST_CASE(test_core_try_load_arg_validation)
{
        seqlock_hdr_t hdr;
        uint64_t payload[WORDS] = {5U, 6U};
        uint64_t out[WORDS];

        seqlock_core_init(&hdr, payload, sizeof(payload));

        TEST_ASSERT(
            seqlock_core_try_load(NULL, payload, out, sizeof(payload), 4U)
            == false);
        TEST_ASSERT(seqlock_core_try_load(&hdr, NULL, out, sizeof(payload), 4U)
                    == false);
        TEST_ASSERT(
            seqlock_core_try_load(&hdr, payload, NULL, sizeof(payload), 4U)
            == false);
        TEST_ASSERT(seqlock_core_try_load(&hdr, payload, out, 0U, 4U) == false);

        /* Zero bound: no attempt, false even though the cell is quiescent. */
        TEST_ASSERT(
            seqlock_core_try_load(&hdr, payload, out, sizeof(payload), 0U)
            == false);
}

int
main(void)
{
        (void)fprintf(stdout, "\n=== seqlock core API tests ===\n\n");

        run_test(test_core_round_trip, "core_round_trip");
        run_test(test_core_init_arg_validation, "core_init_arg_validation");
        run_test(test_core_store_arg_validation, "core_store_arg_validation");
        run_test(test_core_load_arg_validation, "core_load_arg_validation");
        run_test(test_core_try_load_arg_validation,
                 "core_try_load_arg_validation");

        (void)fprintf(stdout, "\n=== all core API tests passed ===\n\n");
        return EXIT_SUCCESS;
}
