/**
 * @file seqlock_conf.h
 * @brief Compile-time configuration for the seqlock library.
 *
 * @details
 *    Every option below is wrapped in an `#ifndef` guard, so a consumer can
 *    override any of them *without forking the library* by defining the macro
 *    first, either with a compiler flag (`-DSEQLOCK_COUNTER_TYPE=uint64_t`) or
 *    by placing its own `config/seqlock_conf.h` earlier on the include path
 *    (as `supervisory-controller` does for `queue_conf.h`). The shipped values
 *    here are the fallback defaults.
 *
 *    Include order: `seqlock_conf.h` is pulled in automatically by `seqlock.h`,
 *    but it is safe (and recommended) to include it explicitly first.
 */
#ifndef SEQLOCK_CONF_H_
#define SEQLOCK_CONF_H_

#include <stdint.h>

/* ================ TUNABLES ================================================ */

/**
 * @def SEQLOCK_COUNTER_TYPE
 * @brief Unsigned integer type of the sequence counter.
 *
 * Defaults to `uint32_t`, which is ample: a torn snapshot could only be
 * *accepted* if the writer completed a full 2^N counter cycle during one
 * reader's payload copy. Override to `uint64_t` to remove even the theoretical
 * concern. Must be lock-free on the target (asserted in seqlock.h).
 */
#ifndef SEQLOCK_COUNTER_TYPE
#define SEQLOCK_COUNTER_TYPE uint32_t
#endif

/**
 * @def SEQLOCK_DEFAULT_MAX_RETRIES
 * @brief Suggested default bound for callers of the bounded `try_load()`.
 *
 * Not consumed by the core directly (the caller passes its own bound); provided
 * so consumers share one project-wide default.
 */
#ifndef SEQLOCK_DEFAULT_MAX_RETRIES
#define SEQLOCK_DEFAULT_MAX_RETRIES 1000u
#endif

/**
 * @def SEQLOCK_CELL_ALIGNED
 * @brief When non-zero, pad each generated cell to a cache line.
 *
 * Set to 1 for array-of-cells use (e.g. one cell per IO channel) so that
 * writes to one cell's counter do not falsely share a cache line with a
 * neighbour. Off by default to keep single-cell instances compact.
 */
#ifndef SEQLOCK_CELL_ALIGNED
#define SEQLOCK_CELL_ALIGNED 0
#endif

/**
 * @def SEQLOCK_CACHELINE
 * @brief Cache-line size (bytes) used when SEQLOCK_CELL_ALIGNED is set.
 */
#ifndef SEQLOCK_CACHELINE
#define SEQLOCK_CACHELINE 64
#endif

/* ================ ATOMIC BACKEND SELECTION ================================ */

/**
 * @def SEQLOCK_USE_GNU_ATOMICS
 * @brief Force the GCC/Clang `__atomic` backend.
 *
 * This is the default backend on GCC and Clang. Define exactly one of
 * SEQLOCK_USE_GNU_ATOMICS, SEQLOCK_USE_C11_ATOMICS, or
 * SEQLOCK_USE_NO_ATOMICS consistently for the seqlock library build and all
 * consumers to override automatic backend selection.
 */

/**
 * @def SEQLOCK_USE_C11_ATOMICS
 * @brief Force the C11 `<stdatomic.h>` backend.
 *
 * Use when the toolchain provides C11 atomics and the project wants to avoid
 * compiler-specific `__atomic` builtins. Define exactly one backend-selection
 * macro consistently for the seqlock library build and all consumers.
 */

/**
 * @def SEQLOCK_USE_NO_ATOMICS
 * @brief Force the degenerate uniprocessor backend.
 *
 * Correct only when readers and the writer cannot run concurrently on separate
 * cores, or on targets with equivalent ordering guarantees. This backend uses
 * a volatile counter and no hardware fences. Define exactly one backend-
 * selection macro consistently for the seqlock library build and all
 * consumers.
 */

/*
 * Pick exactly one backend unless the consumer forced one. The ladder mirrors
 * embedded-queue's queue_conf.h:
 *   GCC/Clang __atomic  ->  C11 <stdatomic.h>  ->  degenerate no-op (uniproc).
 */
#if !defined(SEQLOCK_USE_GNU_ATOMICS) && !defined(SEQLOCK_USE_C11_ATOMICS)     \
    && !defined(SEQLOCK_USE_NO_ATOMICS)
#if defined(__GNUC__) || defined(__clang__)
#define SEQLOCK_USE_GNU_ATOMICS 1
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)               \
    && !defined(__STDC_NO_ATOMICS__)
#define SEQLOCK_USE_C11_ATOMICS 1
#else
#define SEQLOCK_USE_NO_ATOMICS 1
#endif
#endif

#if (defined(SEQLOCK_USE_GNU_ATOMICS) + defined(SEQLOCK_USE_C11_ATOMICS)       \
     + defined(SEQLOCK_USE_NO_ATOMICS))                                        \
    != 1
#error "seqlock: define exactly one atomic backend"
#endif

#if defined(SEQLOCK_USE_GNU_ATOMICS)

/** Qualifier applied to the counter member (none needed for __atomic). */
#define SEQLOCK_ATOMIC_QUAL
#define SEQLOCK_MO_RELAXED                 __ATOMIC_RELAXED
#define SEQLOCK_MO_ACQUIRE                 __ATOMIC_ACQUIRE
#define SEQLOCK_MO_RELEASE                 __ATOMIC_RELEASE
#define SEQLOCK_ATOMIC_LOAD(ptr, mo)       __atomic_load_n((ptr), (mo))
#define SEQLOCK_ATOMIC_STORE(ptr, val, mo) __atomic_store_n((ptr), (val), (mo))
#define SEQLOCK_FENCE_ACQUIRE() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define SEQLOCK_FENCE_RELEASE() __atomic_thread_fence(__ATOMIC_RELEASE)

#elif defined(SEQLOCK_USE_C11_ATOMICS)

#include <stdatomic.h>
#define SEQLOCK_ATOMIC_QUAL          _Atomic
#define SEQLOCK_MO_RELAXED           memory_order_relaxed
#define SEQLOCK_MO_ACQUIRE           memory_order_acquire
#define SEQLOCK_MO_RELEASE           memory_order_release
#define SEQLOCK_ATOMIC_LOAD(ptr, mo) atomic_load_explicit((ptr), (mo))
#define SEQLOCK_ATOMIC_STORE(ptr, val, mo)                                     \
        atomic_store_explicit((ptr), (val), (mo))
#define SEQLOCK_FENCE_ACQUIRE() atomic_thread_fence(memory_order_acquire)
#define SEQLOCK_FENCE_RELEASE() atomic_thread_fence(memory_order_release)

#else /* SEQLOCK_USE_NO_ATOMICS */

/*
 * Degenerate single-core fallback: `volatile` defeats compiler reordering of
 * the counter but provides NO hardware ordering. Correct only on a
 * uniprocessor (or where readers/writer cannot run on separate cores). Fences
 * are no-ops.
 */
#define SEQLOCK_ATOMIC_QUAL                volatile
#define SEQLOCK_MO_RELAXED                 0
#define SEQLOCK_MO_ACQUIRE                 0
#define SEQLOCK_MO_RELEASE                 0
#define SEQLOCK_ATOMIC_LOAD(ptr, mo)       ((void)(mo), (*(ptr)))
#define SEQLOCK_ATOMIC_STORE(ptr, val, mo) ((void)(mo), (void)(*(ptr) = (val)))
#define SEQLOCK_FENCE_ACQUIRE()            ((void)0)
#define SEQLOCK_FENCE_RELEASE()            ((void)0)

#endif

/* ================ SPIN HINT =============================================== */

/**
 * @def SEQLOCK_CPU_RELAX
 * @brief Hint executed on each reader retry to relieve contention.
 *
 * Lowers power and bus traffic while spinning, and on SMT cores yields the
 * pipeline so a co-located writer can make progress. Expands to `pause` on
 * x86, `yield` on AArch64 (GCC/Clang), and to nothing on other targets.
 * Override to supply a platform-specific relax primitive.
 */
#ifndef SEQLOCK_CPU_RELAX
#if defined(__GNUC__) || defined(__clang__)
#if defined(__i386__) || defined(__x86_64__)
#define SEQLOCK_CPU_RELAX() __builtin_ia32_pause()
#elif defined(__aarch64__)
#define SEQLOCK_CPU_RELAX() __asm__ __volatile__("yield")
#else
#define SEQLOCK_CPU_RELAX()                                                    \
        do {                                                                   \
        } while (0)
#endif
#else
#define SEQLOCK_CPU_RELAX()                                                    \
        do {                                                                   \
        } while (0)
#endif
#endif

/* ================ PAYLOAD-COPY STRATEGY (TSAN cleanliness) ================ */

/**
 * @def SEQLOCK_ATOMIC_PAYLOAD_COPY
 * @brief Select the payload-copy mechanism.
 *
 *  - 0 (default off-sanitizer): the payload race is benign by construction, so
 *    the copy is a `memcpy` in a `no_sanitize("thread")` helper. Fast.
 *  - 1: copy the payload byte-wise with `memory_order_relaxed` atomics, which
 *    is *formally* race-free, so ThreadSanitizer is clean with zero annotation
 *    fragility (no reliance on TSAN's memcpy interceptor honouring the
 *    attribute).
 *
 * The default auto-selects (1) whenever ThreadSanitizer is active and (0)
 * otherwise, giving both production speed and a genuinely clean TSAN test.
 * Override to pin a strategy.
 */
#ifndef SEQLOCK_ATOMIC_PAYLOAD_COPY
#if defined(__SANITIZE_THREAD__)
#define SEQLOCK_ATOMIC_PAYLOAD_COPY 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define SEQLOCK_ATOMIC_PAYLOAD_COPY 1
#endif
#endif
#endif
#ifndef SEQLOCK_ATOMIC_PAYLOAD_COPY
#define SEQLOCK_ATOMIC_PAYLOAD_COPY 0
#endif

/**
 * @def SEQLOCK_NO_SANITIZE_THREAD
 * @brief Attribute that marks the (benign) payload-copy helper, or nothing.
 */
#ifndef SEQLOCK_NO_SANITIZE_THREAD
#if defined(__has_attribute)
#if __has_attribute(no_sanitize)
#define SEQLOCK_NO_SANITIZE_THREAD __attribute__((no_sanitize("thread")))
#endif
#endif
#endif
#ifndef SEQLOCK_NO_SANITIZE_THREAD
#define SEQLOCK_NO_SANITIZE_THREAD
#endif

#endif /* SEQLOCK_CONF_H_ */
