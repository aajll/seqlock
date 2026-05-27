/*
 * @file test_seqlock.c
 * @brief Single-threaded behavioural tests for the seqlock primitive.
 *
 * Covers round-trip / latest, pointer validation, even-counter invariant,
 * large payload, and init/zero snapshot.
 */

#include "seqlock.h"
#include "seqlock_test.h"

#include <stdint.h>
#include <string.h>

/* Small tuple mirroring the first consumer's cache cell. */
typedef struct {
        float value;
        uint64_t timestamp_ns;
        bool valid;
} sample_t;

SEQLOCK_DEFINE(sample_cell, sample_t)

/* Payload larger than a cache line, to exercise multi-word consistency. */
typedef struct {
        uint64_t words[24]; /* 192 bytes */
} big_t;

SEQLOCK_DEFINE(big_cell, big_t)

static bool
sample_eq(const sample_t *a, const sample_t *b)
{
        return a->value == b->value && a->timestamp_ns == b->timestamp_ns
               && a->valid == b->valid;
}

TEST_CASE(test_init_zero_snapshot)
{
        sample_cell_t cell;
        sample_t out;

        (void)memset(&out, 0xAA, sizeof(out));
        sample_cell_init(&cell);

        /* Counter even after init. */
        TEST_ASSERT((cell.hdr.seq & 1U) == 0U);

        /* A never-written cell yields a defined all-zero snapshot. */
        sample_cell_load(&cell, &out);
        TEST_ASSERT(out.value == 0.0f);
        TEST_ASSERT(out.timestamp_ns == 0U);
        TEST_ASSERT(out.valid == false);
}

TEST_CASE(test_round_trip)
{
        sample_cell_t cell;
        sample_t in = {3.5f, 1234567890ULL, true};
        sample_t out;

        sample_cell_init(&cell);
        sample_cell_store(&cell, &in);
        sample_cell_load(&cell, &out);

        TEST_ASSERT(sample_eq(&in, &out));
        /* Counter even after a completed store. */
        TEST_ASSERT((cell.hdr.seq & 1U) == 0U);
}

TEST_CASE(test_repeated_stores_latest)
{
        sample_cell_t cell;
        sample_t out;
        uint32_t i;

        sample_cell_init(&cell);
        for (i = 0U; i < 1000U; ++i) {
                sample_t in = {(float)i, (uint64_t)i * 10U, (i & 1U) != 0U};
                sample_cell_store(&cell, &in);
        }

        sample_cell_load(&cell, &out);
        TEST_ASSERT(out.value == 999.0f);
        TEST_ASSERT(out.timestamp_ns == 9990U);
        TEST_ASSERT(out.valid == true);
        TEST_ASSERT((cell.hdr.seq & 1U) == 0U);
}

TEST_CASE(test_even_counter_advances_by_two)
{
        sample_cell_t cell;
        sample_t in = {1.0f, 1U, true};
        SEQLOCK_COUNTER_TYPE before;

        sample_cell_init(&cell);
        before = cell.hdr.seq;
        sample_cell_store(&cell, &in);
        TEST_ASSERT(cell.hdr.seq == (SEQLOCK_COUNTER_TYPE)(before + 2U));
        TEST_ASSERT((cell.hdr.seq & 1U) == 0U);
}

TEST_CASE(test_null_validation)
{
        sample_cell_t cell;
        sample_t in = {7.0f, 7U, true};
        sample_t out;

        sample_cell_init(&cell);
        sample_cell_store(&cell, &in);

        /* NULL cell / payload must be safe no-ops, never crash. */
        sample_cell_init(NULL);
        sample_cell_store(NULL, &in);
        sample_cell_store(&cell, NULL);
        sample_cell_load(NULL, &out);
        sample_cell_load(&cell, NULL);

        /* load(NULL cell) must zero a defined snapshot, not leave garbage. */
        (void)memset(&out, 0x5A, sizeof(out));
        sample_cell_load(NULL, &out);
        TEST_ASSERT(out.value == 0.0f && out.timestamp_ns == 0U
                    && out.valid == false);

        /* The original cell is untouched by the NULL calls. */
        sample_cell_load(&cell, &out);
        TEST_ASSERT(sample_eq(&in, &out));

        TEST_ASSERT(sample_cell_try_load(NULL, &out, 4U) == false);
}

TEST_CASE(test_large_payload)
{
        big_cell_t cell;
        big_t in;
        big_t out;
        size_t i;

        for (i = 0U; i < 24U; ++i) {
                in.words[i] = 0x1122334455667788ULL ^ (uint64_t)i;
        }

        big_cell_init(&cell);
        big_cell_store(&cell, &in);
        big_cell_load(&cell, &out);

        for (i = 0U; i < 24U; ++i) {
                TEST_ASSERT(out.words[i] == in.words[i]);
        }
}

int
main(void)
{
        (void)fprintf(stdout, "\n=== seqlock basic tests ===\n\n");

        run_test(test_init_zero_snapshot, "init_zero_snapshot");
        run_test(test_round_trip, "round_trip");
        run_test(test_repeated_stores_latest, "repeated_stores_latest");
        run_test(test_even_counter_advances_by_two, "even_counter_advances");
        run_test(test_null_validation, "null_validation");
        run_test(test_large_payload, "large_payload");

        (void)fprintf(stdout, "\n=== all basic tests passed ===\n\n");
        return EXIT_SUCCESS;
}
