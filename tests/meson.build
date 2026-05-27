thread_dep = dependency('threads')

# Single-threaded behavioural tests: round-trip, repeated stores, init/zero
# snapshot, pointer validation, even-counter invariant, large payload.
test_basic_exe = executable(
  'test_seqlock',
  files('test_seqlock.c'),
  dependencies: [seqlock_dep],
  include_directories: [public_headers, generated_headers],
)
test('seqlock basic', test_basic_exe)

# Direct type-erased core API: round-trip and argument validation.
test_core_exe = executable(
  'test_seqlock_core',
  files('test_seqlock_core.c'),
  dependencies: [seqlock_dep],
  include_directories: [public_headers, generated_headers],
)
test('seqlock core', test_core_exe)

# Bounded try_load: quiescent success, zero-bound, stuck-odd exhaustion.
test_try_exe = executable(
  'test_seqlock_try_load',
  files('test_seqlock_try_load.c'),
  dependencies: [seqlock_dep],
  include_directories: [public_headers, generated_headers],
)
test('seqlock try_load', test_try_exe)

# Counter wrap across UINT32_MAX -> 0.
test_wrap_exe = executable(
  'test_seqlock_wrap',
  files('test_seqlock_wrap.c'),
  dependencies: [seqlock_dep],
  include_directories: [public_headers, generated_headers],
)
test('seqlock wrap', test_wrap_exe)

# C11 <stdatomic.h> backend. The default on GCC/Clang is the __atomic path, so
# this forces the C11 backend for both the test and the core (compiled from
# library_sources with the same -D) to exercise that path and its lock-free
# static assertion.
test_c11_exe = executable(
  'test_seqlock_c11',
  files('test_seqlock_c11.c') + library_sources,
  c_args: ['-DSEQLOCK_USE_C11_ATOMICS=1'],
  include_directories: [public_headers, generated_headers],
)
test('seqlock c11', test_c11_exe)

# Concurrent torn-read detection + wait-free-writer liveness + large payload.
# This is also the executable rebuilt under -Db_sanitize=thread for the
# TSAN-clean concurrency gate (separate CI job; see ci.yml).
test_mt_exe = executable(
  'test_seqlock_mt',
  files('test_seqlock_mt.c'),
  dependencies: [seqlock_dep, thread_dep],
  include_directories: [public_headers, generated_headers],
)
test('seqlock mt', test_mt_exe, timeout: 120)

# Long-running multi-reader soak with the self-checking invariant.
test_soak_exe = executable(
  'test_seqlock_soak',
  files('test_seqlock_soak.c'),
  dependencies: [seqlock_dep, thread_dep],
  include_directories: [public_headers, generated_headers],
)
test('seqlock soak', test_soak_exe, timeout: 180)
