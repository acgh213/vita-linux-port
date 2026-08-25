#!/bin/sh
set -eu

src=${1:-cart/src/pstv-demo-cart.c}

if grep -q 'clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME' "$src"; then
    printf '%s\n' 'FAIL runtime loop must not submit a stale absolute deadline'
    exit 1
fi
if ! grep -q 'clock_nanosleep(CLOCK_MONOTONIC,0,&delay,NULL)' "$src"; then
    printf '%s\n' 'FAIL runtime loop must sleep relative to the post-render tick'
    exit 1
fi
if [ "$(grep -c 'cart_runtime_tick(&runtime, now_ns) != 0' "$src")" -ne 2 ]; then
    printf '%s\n' 'FAIL runtime loop must tick once before rendering and once before sleep'
    exit 1
fi

printf '%s\n' 'runtime loop deadline contract passed'
