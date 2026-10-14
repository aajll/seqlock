# MISRA baseline

Each atomic backend has its own baseline, because the findings differ between them:

- `misra-baseline.json` — the GCC/Clang `__atomic` backend (base configuration).
- `misra-baseline.c11.json` — the C11 `<stdatomic.h>` backend (`--profile c11`).
- `misra-baseline.no_atomics.json` — the uniprocessor `volatile` backend (`--profile no_atomics`).

CI runs `misch run --baseline` for each, and fails on any finding not recorded in that backend's baseline. Regenerate a baseline only after a deliberate review, with `misch baseline` and the same `--profile`; do not edit it by hand.
