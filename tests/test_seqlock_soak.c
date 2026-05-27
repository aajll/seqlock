/*
 * @file test_seqlock_soak.c
 * @brief Long-running multi-reader soak.
 *
 * Same self-checking invariant as the mt test, run for a large iteration
 * budget with several readers to shake out rare interleavings. Parameters are
 * compile-time constants so CI can shrink them via -D overrides.
 */

#include "seqlock.h"
#include "seqlock_test.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#ifndef SOAK_READERS
#define SOAK_READERS 4
#endif

/* Scaled down under sanitizers (see seqlock_test.h); overridable via -D. */
#ifndef SOAK_WRITER_ITERS
#if SEQLOCK_TEST_SANITIZED
#define SOAK_WRITER_ITERS 300000ULL
#else
#define SOAK_WRITER_ITERS 5000000ULL
#endif
#endif

#define SOAK_WORDS 7U
#define SOAK_PRIME 2654435761ULL

typedef struct {
        uint64_t seed;
        uint64_t words[SOAK_WORDS];
} soak_t;

SEQLOCK_DEFINE(soak_cell, soak_t)

static soak_cell_t g_cell;
static atomic_bool g_done;
static atomic_ullong g_reads;

static void *
soak_writer(void *arg)
{
        uint64_t n;

        (void)arg;
        for (n = 1ULL; n <= SOAK_WRITER_ITERS; ++n) {
                soak_t in;
                uint32_t i;
                in.seed = n;
                for (i = 0U; i < SOAK_WORDS; ++i) {
                        in.words[i] = n ^ ((uint64_t)i * SOAK_PRIME);
                }
                soak_cell_store(&g_cell, &in);
        }
        atomic_store_explicit(&g_done, true, memory_order_release);
        return NULL;
}

static void *
soak_reader(void *arg)
{
        unsigned long long reads = 0ULL;

        (void)arg;
        while (!atomic_load_explicit(&g_done, memory_order_acquire)) {
                soak_t out;
                uint32_t i;
                soak_cell_load(&g_cell, &out);
                for (i = 0U; i < SOAK_WORDS; ++i) {
                        TEST_ASSERT(out.words[i]
                                    == (out.seed ^ ((uint64_t)i * SOAK_PRIME)));
                }
                ++reads;
        }
        atomic_fetch_add_explicit(&g_reads, reads, memory_order_relaxed);
        return NULL;
}

int
main(void)
{
        pthread_t writer;
        pthread_t readers[SOAK_READERS];
        int i;

        (void)fprintf(stdout,
                      "\n=== seqlock soak (%d readers, %llu writes) ===\n\n",
                      SOAK_READERS, (unsigned long long)SOAK_WRITER_ITERS);

        soak_cell_init(&g_cell);
        atomic_store_explicit(&g_done, false, memory_order_relaxed);
        atomic_store_explicit(&g_reads, 0ULL, memory_order_relaxed);

        /* Publish a valid probe before readers start (init's zero state does
         * not satisfy the invariant). */
        {
                soak_t seed0;
                uint32_t k;
                seed0.seed = 0ULL;
                for (k = 0U; k < SOAK_WORDS; ++k) {
                        seed0.words[k] = 0ULL ^ ((uint64_t)k * SOAK_PRIME);
                }
                soak_cell_store(&g_cell, &seed0);
        }

        for (i = 0; i < SOAK_READERS; ++i) {
                TEST_ASSERT(pthread_create(&readers[i], NULL, soak_reader, NULL)
                            == 0);
        }
        TEST_ASSERT(pthread_create(&writer, NULL, soak_writer, NULL) == 0);

        TEST_ASSERT(pthread_join(writer, NULL) == 0);
        for (i = 0; i < SOAK_READERS; ++i) {
                TEST_ASSERT(pthread_join(readers[i], NULL) == 0);
        }

        TEST_ASSERT(atomic_load_explicit(&g_reads, memory_order_relaxed)
                    > 0ULL);

        (void)fprintf(stdout, "soak ok: total reads=%llu\n",
                      (unsigned long long)atomic_load_explicit(
                          &g_reads, memory_order_relaxed));
        (void)fprintf(stdout, "\n=== soak test passed ===\n\n");
        return EXIT_SUCCESS;
}
