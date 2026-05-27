/*
 * @file seqlock_test.h
 * @brief Minimal assert-based test harness shared by the seqlock tests.
 */

#ifndef SEQLOCK_TEST_H_
#define SEQLOCK_TEST_H_

#include <stdio.h>
#include <stdlib.h>

/*
 * SEQLOCK_TEST_SANITIZED is 1 under ThreadSanitizer or AddressSanitizer.
 * Concurrency tests use it to shrink their iteration budget: sanitizers add a
 * 10-50x slowdown, and race / torn-read detection comes from interleavings,
 * not from sheer volume, so a smaller budget is just as effective and keeps CI
 * wall-clock bounded. Native builds keep the full (heavier) budget.
 */
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
#define SEQLOCK_TEST_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#define SEQLOCK_TEST_SANITIZED 1
#endif
#endif
#ifndef SEQLOCK_TEST_SANITIZED
#define SEQLOCK_TEST_SANITIZED 0
#endif

#define TEST_ASSERT(expr)                                                      \
        do {                                                                   \
                if (!(expr)) {                                                 \
                        (void)fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__,   \
                                      __LINE__, #expr);                        \
                        exit(EXIT_FAILURE);                                    \
                }                                                              \
        } while (0)

#define TEST_PASS(name) (void)fprintf(stdout, "PASS  %s\n", (name))

#define TEST_CASE(name)                                                        \
        static void name(void);                                                \
        static void name(void)

static inline void
run_test(void (*test_func)(void), const char *name)
{
        test_func();
        TEST_PASS(name);
}

#endif /* SEQLOCK_TEST_H_ */
