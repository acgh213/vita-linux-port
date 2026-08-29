#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
TOOL="$ROOT/toolkit/bin/vita-bench"
SOURCE="$ROOT/toolkit/share/vita-bench.c"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0 HUP INT TERM

mkdir -p "$TMP/root/sys/devices/system/cpu" "$TMP/toolkit/bin" "$TMP/toolkit/share"
printf '0-3\n' > "$TMP/root/sys/devices/system/cpu/online"

# Compile the benchmark core as a host test binary. This is the real workload
# implementation, not a stub or output-only shell fixture.
cc -std=c11 -Wall -Wextra -Werror -pthread "$SOURCE" -o "$TMP/vita-bench-core"

out=$(VITA_BENCH_ROOT="$TMP/root" "$TMP/vita-bench-core" \
    --machine --quick --workers 1)
printf '%s\n' "$out"
printf '%s\n' "$out" | grep -F 'schema=1' >/dev/null
printf '%s\n' "$out" | grep -F 'online_cpus=0-3' >/dev/null
printf '%s\n' "$out" | grep -F 'workers_requested=1' >/dev/null
printf '%s\n' "$out" | grep -F 'compute_1_workers_mops=' >/dev/null
printf '%s\n' "$out" | grep -F 'memcpy_1_workers_mib_s=' >/dev/null
printf '%s\n' "$out" | grep -F 'memset_1_workers_mib_s=' >/dev/null
printf '%s\n' "$out" | grep -F 'framebuffer=skipped' >/dev/null

out4=$(VITA_BENCH_ROOT="$TMP/root" "$TMP/vita-bench-core" \
    --machine --quick --workers 4)
printf '%s\n' "$out4" | grep -F 'workers_requested=4' >/dev/null
printf '%s\n' "$out4" | grep -F 'compute_4_workers_mops=' >/dev/null
printf '%s\n' "$out4" | grep -F 'memcpy_4_workers_mib_s=' >/dev/null
printf '%s\n' "$out4" | grep -F 'memset_4_workers_mib_s=' >/dev/null
printf '%s\n' "$out4" | grep -F 'framebuffer=skipped' >/dev/null

# The installed wrapper must invoke the payload benchmark binary, while
# leaving framebuffer access opt-in.
# shellcheck disable=SC2016 # The generated fixture must expand variables when it runs.
printf '#!/bin/sh\nprintf "%%s\\n" "$*" > "$VITA_BENCH_TEST_LOG"\nprintf "binary_ok\\n"\n' > "$TMP/fake-bench"
chmod +x "$TMP/fake-bench"
mkdir -p "$TMP/toolkit/libexec"
cp "$TMP/fake-bench" "$TMP/toolkit/libexec/vita-bench.arm"
wrapper_out=$(VITA_BENCH_ROOT="$TMP/toolkit" \
    VITA_BENCH_BINARY="$TMP/toolkit/libexec/vita-bench.arm" \
    VITA_BENCH_TEST_LOG="$TMP/wrapper.log" \
    "$TOOL" --machine --quick)
printf '%s\n' "$wrapper_out" | grep -F 'binary_ok' >/dev/null
grep -F -- '--machine --quick' "$TMP/wrapper.log" >/dev/null
if grep -F -- '--framebuffer' "$TMP/wrapper.log" >/dev/null; then
    printf 'FAIL: default wrapper enabled framebuffer access\n' >&2
    exit 1
fi

printf 'vita-bench test passed\n'
