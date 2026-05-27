/*
 * @file test_seqlock_mt.c
 * @brief Concurrent torn-read detection + wait-free-writer liveness.
 *
 * Covers torn-read detection, writer liveness, and large payloads. This same
 * executable, rebuilt with -Db_sanitize=thread, is the TSAN-clean gate.
 *
 * The payload carries a self-checking invariant: every word is derived from a
 * single seed, so any torn tuple (a mix of two writers' words) is detected by
 * the reader. Readers assert the invariant on every accepted snapshot.
 */

#include "seqlock.h"
#include "seqlock_test.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#define PROBE_WORDS 15U
#define PRIME       1099511628211ULL /* FNV-1a 64-bit prime */
#define NUM_READERS 3

/* Scaled down under sanitizers (see seqlock_test.h); overridable via -D. */
#ifndef WRITER_ITERS
#if SEQLOCK_TEST_SANITIZED
#define WRITER_ITERS 100000ULL
#else
#define WRITER_ITERS 2000000ULL
#endif
#endif

/* 128 bytes: spans multiple cache lines. words[i] == seed + i*PRIME. */
typedef struct {
        uint64_t seed;
        uint64_t words[PROBE_WORDS];
} probe_t;

SEQLOCK_DEFINE(probe_cell, probe_t)

static probe_cell_t g_cell;
static atomic_bool g_writer_done;
static atomic_ullong g_total_reads;

static void
probe_fill(probe_t *p, uint64_t seed)
{
        uint32_t i;

        p->seed = seed;
        for (i = 0U; i < PROBE_WORDS; ++i) {
                p->words[i] = seed + ((uint64_t)i * PRIME);
        }
}

static void
probe_check(const probe_t *p)
{
        uint32_t i;

        for (i = 0U; i < PROBE_WORDS; ++i) {
                /* A torn read would pair words from different seeds. */
                TEST_ASSERT(p->words[i] == p->seed + ((uint64_t)i * PRIME));
        }
}

static void *
writer_thread(void *arg)
{
        uint64_t n;

        (void)arg;
        for (n = 1ULL; n <= WRITER_ITERS; ++n) {
                probe_t in;
                probe_fill(&in, n);
                probe_cell_store(&g_cell, &in);
        }
        atomic_store_explicit(&g_writer_done, true, memory_order_release);
        return NULL;
}

static void *
reader_thread(void *arg)
{
        unsigned long long reads = 0ULL;

        (void)arg;
        while (!atomic_load_explicit(&g_writer_done, memory_order_acquire)) {
                probe_t out;
                probe_cell_load(&g_cell, &out);
                probe_check(&out);
                ++reads;
        }
        /* A few more after the writer stops, to read the final value. */
        {
                probe_t out;
                probe_cell_load(&g_cell, &out);
                probe_check(&out);
                ++reads;
        }
        atomic_fetch_add_explicit(&g_total_reads, reads, memory_order_relaxed);
        return NULL;
}

/*
 * Reader that uses the bounded try_load() against the hot writer. With a
 * generous bound it succeeds on most calls; under contention it exercises both
 * the "counter moved during copy" retry and the success-after-retry path. Every
 * accepted snapshot must still satisfy the invariant.
 */
static void *
tryload_reader_thread(void *arg)
{
        unsigned long long reads = 0ULL;

        (void)arg;
        while (!atomic_load_explicit(&g_writer_done, memory_order_acquire)) {
                probe_t out;
                if (probe_cell_try_load(&g_cell, &out, 1000U)) {
                        probe_check(&out);
                        ++reads;
                }
        }
        atomic_fetch_add_explicit(&g_total_reads, reads, memory_order_relaxed);
        return NULL;
}

int
main(void)
{
        pthread_t writer;
        pthread_t readers[NUM_READERS];
        pthread_t try_readers[2];
        probe_t final_out;
        int i;

        (void)fprintf(stdout,
                      "\n=== seqlock concurrent torn-read test ===\n\n");

        probe_cell_init(&g_cell);
        atomic_store_explicit(&g_writer_done, false, memory_order_relaxed);
        atomic_store_explicit(&g_total_reads, 0ULL, memory_order_relaxed);

        /* Publish a valid probe before readers start: the all-zero init state
         * does not satisfy the words[i] == seed + i*PRIME invariant, and a
         * reader may run before the writer's first store. */
        {
                probe_t seed0;
                probe_fill(&seed0, 0ULL);
                probe_cell_store(&g_cell, &seed0);
        }

        for (i = 0; i < NUM_READERS; ++i) {
                TEST_ASSERT(
                    pthread_create(&readers[i], NULL, reader_thread, NULL)
                    == 0);
        }
        for (i = 0; i < 2; ++i) {
                TEST_ASSERT(pthread_create(&try_readers[i], NULL,
                                           tryload_reader_thread, NULL)
                            == 0);
        }
        TEST_ASSERT(pthread_create(&writer, NULL, writer_thread, NULL) == 0);

        TEST_ASSERT(pthread_join(writer, NULL) == 0);
        for (i = 0; i < NUM_READERS; ++i) {
                TEST_ASSERT(pthread_join(readers[i], NULL) == 0);
        }
        for (i = 0; i < 2; ++i) {
                TEST_ASSERT(pthread_join(try_readers[i], NULL) == 0);
        }

        /* Writer liveness: the final published value is the last seed. */
        probe_cell_load(&g_cell, &final_out);
        probe_check(&final_out);
        TEST_ASSERT(final_out.seed == WRITER_ITERS);

        /* Readers made progress (were not starved). */
        TEST_ASSERT(atomic_load_explicit(&g_total_reads, memory_order_relaxed)
                    > 0ULL);

        (void)fprintf(stdout,
                      "writer iters=%llu  total reads=%llu (no torn reads)\n",
                      (unsigned long long)WRITER_ITERS,
                      (unsigned long long)atomic_load_explicit(
                          &g_total_reads, memory_order_relaxed));
        (void)fprintf(stdout, "\n=== concurrent test passed ===\n\n");
        return EXIT_SUCCESS;
}
