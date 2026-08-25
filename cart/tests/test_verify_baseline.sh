#!/bin/sh
# Isolated fixture tests for cart/scripts/verify-baseline.sh.
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
VERIFIER=$SCRIPT_DIR/../scripts/verify-baseline.sh
TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/cart-verify-baseline.XXXXXX") || exit 1

cleanup() {
    rm -rf "$TMP_ROOT"
}
trap cleanup 0 HUP INT TERM

fail() {
    printf 'not ok - %s\n' "$1" >&2
    exit 1
}

assert_contains() {
    haystack=$1
    needle=$2
    case $haystack in
        *"$needle"*) ;;
        *) fail "expected output to contain: $needle" ;;
    esac
}

assert_empty_file() {
    file=$1
    [ ! -s "$file" ] || fail "expected $file to be empty"
}

run_help_diagnostic_test() {
    output=$TMP_ROOT/help-output
    error=$TMP_ROOT/help-error
    if "$VERIFIER" --help >"$output" 2>"$error"; then
        status=0
    else
        status=$?
    fi
    [ "$status" -eq 0 ] || fail "--help exited with status $status"
    assert_contains "$(cat "$output")" 'Usage: verify-baseline.sh'
    assert_empty_file "$error"
    printf 'ok - --help uses stdout and status 0\n'
}

run_invalid_diagnostic_test() {
    label=$1
    shift
    output=$TMP_ROOT/invalid-output
    error=$TMP_ROOT/invalid-error
    if "$VERIFIER" "$@" >"$output" 2>"$error"; then
        fail "$label unexpectedly passed"
    else
        status=$?
    fi
    [ "$status" -eq 2 ] || fail "$label exited with status $status"
    assert_contains "$(cat "$error")" 'Usage: verify-baseline.sh'
    assert_empty_file "$output"
    printf 'ok - %s uses stderr and status 2\n' "$label"
}

run_ok() {
    label=$1
    shift
    output=$TMP_ROOT/output
    error=$TMP_ROOT/error
    if ! PATH="$TOOLS:$PATH" "$@" --arm-binary "$ARM_BINARY" --fixture-tools "$TOOLS" >"$output" 2>"$error"; then
        cat "$output" "$error" >&2
        fail "$label"
    fi
    assert_contains "$(cat "$output")" 'PASS baseline.schema_version'
    printf 'ok - %s\n' "$label"
}

run_fail() {
    label=$1
    needle=$2
    shift 2
    output=$TMP_ROOT/output
    error=$TMP_ROOT/error
    if [ "$label" = 'ARM binary is explicitly required' ]; then
        if "$@" >"$output" 2>"$error"; then
            cat "$output" "$error" >&2
            fail "$label unexpectedly passed"
        fi
    elif PATH="$TOOLS:$PATH" "$@" --arm-binary "$ARM_BINARY" --fixture-tools "$TOOLS" >"$output" 2>"$error"; then
        cat "$output" "$error" >&2
        fail "$label unexpectedly passed"
    fi
    assert_contains "$(cat "$output" "$error")" "$needle"
    printf 'ok - %s\n' "$label"
}

make_fixture() {
    name=$1
    FIXTURE=$TMP_ROOT/$name
    KERNEL=$FIXTURE/kernel-source
    LOADER=$FIXTURE/loader-source
    OUTER=$FIXTURE/outer
    mkdir -p "$KERNEL" "$LOADER" "$OUTER"

    git -C "$KERNEL" init -q
    git -C "$KERNEL" config user.email test@example.invalid
    git -C "$KERNEL" config user.name Fixture
    printf 'kernel one\n' >"$KERNEL/state"
    git -C "$KERNEL" add state
    git -C "$KERNEL" commit -qm 'kernel fixture one'
    KERNEL_FIRST=$(git -C "$KERNEL" rev-parse HEAD)
    printf 'kernel two\n' >"$KERNEL/state"
    git -C "$KERNEL" commit -qam 'kernel fixture two'
    KERNEL_HEAD=$(git -C "$KERNEL" rev-parse HEAD)

    git -C "$LOADER" init -q
    git -C "$LOADER" config user.email test@example.invalid
    git -C "$LOADER" config user.name Fixture
    printf 'loader\n' >"$LOADER/state"
    git -C "$LOADER" add state
    git -C "$LOADER" commit -qm 'loader fixture'
    LOADER_HEAD=$(git -C "$LOADER" rev-parse HEAD)

    git -C "$OUTER" init -q
    git -C "$OUTER" config user.email test@example.invalid
    git -C "$OUTER" config user.name Fixture
    git -C "$OUTER" -c protocol.file.allow=always submodule add -q "$KERNEL" linux_vita
    git -C "$OUTER" -c protocol.file.allow=always submodule add -q "$LOADER" vita-baremetal-linux-loader
    git -C "$OUTER" add .gitmodules linux_vita vita-baremetal-linux-loader
    git -C "$OUTER" commit -qm 'fixture production tree'
    OUTER_HEAD=$(git -C "$OUTER" rev-parse HEAD)

    CART=$OUTER/cart
    FROZEN=$CART/provenance/v0.1
    mkdir -p "$FROZEN" "$CART/src" "$FIXTURE/tools"
    printf 'frozen renderer\n' >"$FROZEN/pstv-demo-cart.c"
    printf 'frozen start wrapper\n' >"$FROZEN/start-demo-cart.sh"
    printf 'frozen stop wrapper\n' >"$FROZEN/stop-demo-cart.sh"
    printf 'frozen readme\n' >"$FROZEN/README.md"
    cp "$FROZEN/pstv-demo-cart.c" "$CART/src/pstv-demo-cart.c"
    ARM_BINARY=$FIXTURE/pstv-demo-cart
    printf 'fixture ARM binary\n' >"$ARM_BINARY"
    chmod +x "$ARM_BINARY"
    TOOLS=$FIXTURE/tools
    export FIXTURE_ARM_BINARY="$ARM_BINARY"
    cat >"$TOOLS/readelf" <<'EOF'
#!/bin/sh
case $1 in
    -h)
        case $2 in
            *non-arm*) printf '%s\n' '  Class:                             ELF32' '  Data:                              2\047s complement, little endian' '  Type:                              EXEC (Executable file)' '  Machine:                           Advanced Micro Devices X86-64' '  Flags:                             0x0' ;;
            *) printf '%s\n' '  Class:                             ELF32' '  Data:                              2\047s complement, little endian' '  Type:                              EXEC (Executable file)' '  Machine:                           ARM' '  Flags:                             0x5000400, Version5 EABI, hard-float ABI' ;;
        esac
        ;;
    -l)
        case $2 in
            *dynamic*) printf '%s\n' '  INTERP         0x000134 0x00010134 0x00010134 0x00013 0x00013 R   0x1' ;;
            *) printf '%s\n' 'There are no program headers in this file.' ;;
        esac
        ;;
    -d)
        case $2 in
            *dynamic*) printf '%s\n' 'Dynamic section at offset 0x100 contains 1 entry:' ;;
            *) printf '%s\n' 'There is no dynamic section in this file.' ;;
        esac
        ;;
    *) exit 2 ;;
esac
EOF
    cat >"$TOOLS/file" <<'EOF'
#!/bin/sh
[ "$1" != -b ] || shift
printf '%s: ELF 32-bit LSB executable, ARM, EABI5 version 1 (GNU/Linux), statically linked\n' "$1"
EOF
    chmod +x "$TOOLS/readelf" "$TOOLS/file"
    FROZEN_C_SHA256=$(sha256sum "$FROZEN/pstv-demo-cart.c" | awk '{ print $1 }')
    FROZEN_START_SHA256=$(sha256sum "$FROZEN/start-demo-cart.sh" | awk '{ print $1 }')
    FROZEN_STOP_SHA256=$(sha256sum "$FROZEN/stop-demo-cart.sh" | awk '{ print $1 }')
    FROZEN_README_SHA256=$(sha256sum "$FROZEN/README.md" | awk '{ print $1 }')
    ARM_BINARY_SHA256=$(sha256sum "$ARM_BINARY" | awk '{ print $1 }')
    BASELINE=$FIXTURE/baseline.txt
    EXPECTED=$FIXTURE/expected.txt
    write_values "$BASELINE" "$OUTER_HEAD" "$KERNEL_HEAD" "$LOADER_HEAD"
    cp "$BASELINE" "$EXPECTED"
}

write_values() {
    file=$1
    outer=$2
    kernel=$3
    loader=$4
    cat >"$file" <<EOF
schema_version=1
production_outer_commit=$outer
production_kernel_gitlink=$kernel
production_loader_gitlink=$loader
known_good_pstv_demo_cart_c_sha256=$FROZEN_C_SHA256
known_good_start_demo_cart_sh_sha256=$FROZEN_START_SHA256
known_good_stop_demo_cart_sh_sha256=$FROZEN_STOP_SHA256
known_good_readme_md_sha256=$FROZEN_README_SHA256
known_good_demo_binary_sha256=$ARM_BINARY_SHA256
target_model=pstv
target_framebuffer=1280x720
target_logical_render=320x180
target_fps=30.04
tinygl_tested_commit=5555555555555555555555555555555555555555
EOF
}

mkdir -p "$TMP_ROOT"

run_help_diagnostic_test
run_invalid_diagnostic_test 'invalid option' --bogus
run_invalid_diagnostic_test 'missing --repo argument' --repo

make_fixture matching
run_fail 'test hook requires explicit nonproduction override' 'expected constants require --allow-nonproduction' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED"
run_fail 'ARM binary is explicitly required' 'FAIL arm_binary.required' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED" --allow-nonproduction
run_ok 'matching production-shaped local fixture' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED" --allow-nonproduction
printf 'mismatch\n' >>"$ARM_BINARY"
run_fail 'ARM binary hash mismatch' 'FAIL arm_binary.sha256' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED" --allow-nonproduction
printf 'fixture ARM binary\n' >"$ARM_BINARY"
cp "$ARM_BINARY" "$FIXTURE/non-arm-binary"
SAVED_ARM_BINARY=$ARM_BINARY
ARM_BINARY=$FIXTURE/non-arm-binary
run_fail 'non-ARM binary is rejected' 'FAIL arm_binary.machine' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED" --allow-nonproduction
ARM_BINARY=$SAVED_ARM_BINARY
printf 'active behavior-preserving refactor\n' >>"$CART/src/pstv-demo-cart.c"
run_ok 'active refactor source is allowed while v0.1 remains frozen' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED" --allow-nonproduction
printf 'mutated frozen renderer\n' >>"$FROZEN/pstv-demo-cart.c"
run_fail 'mutated frozen renderer is rejected' 'FAIL provenance.v0_1.pstv_demo_cart_c_sha256' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED" --allow-nonproduction

make_fixture outer-mismatch
EXPECTED_BAD=$FIXTURE/expected-bad.txt
write_values "$EXPECTED_BAD" "1111111111111111111111111111111111111111" "$KERNEL_HEAD" "$LOADER_HEAD"
write_values "$BASELINE" "1111111111111111111111111111111111111111" "$KERNEL_HEAD" "$LOADER_HEAD"
run_fail 'outer ancestry mismatch' 'FAIL outer.ancestor' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED_BAD" --allow-nonproduction

make_fixture gitlink-mismatch
EXPECTED_BAD=$FIXTURE/expected-bad.txt
write_values "$EXPECTED_BAD" "$OUTER_HEAD" "$KERNEL_FIRST" "$LOADER_HEAD"
write_values "$BASELINE" "$OUTER_HEAD" "$KERNEL_FIRST" "$LOADER_HEAD"
run_fail 'recorded kernel gitlink mismatch' 'FAIL outer.kernel_gitlink' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED_BAD" --allow-nonproduction

make_fixture checkout-mismatch
git -C "$OUTER/linux_vita" checkout -q "$KERNEL_FIRST"
run_fail 'checked out kernel commit mismatch' 'FAIL submodule.kernel_commit' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED" --allow-nonproduction

make_fixture missing-submodule
rm -rf "$OUTER/linux_vita"
run_fail 'missing submodule directory' 'FAIL submodule.kernel_initialized' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED" --allow-nonproduction

make_fixture symlink-escape
rm -rf "$OUTER/linux_vita"
ln -s "$KERNEL" "$OUTER/linux_vita"
run_fail 'symlinked submodule escapes outer worktree' 'FAIL submodule.kernel_initialized' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED" --allow-nonproduction

make_fixture dirty-submodule
printf 'dirty\n' >"$OUTER/linux_vita/untracked-file"
run_fail 'dirty submodule' 'FAIL submodule.kernel_clean' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED" --allow-nonproduction

make_fixture malformed-baseline
MALFORMED=$FIXTURE/malformed.txt
awk -F= '$1 != "target_fps"' "$BASELINE" >"$MALFORMED"
run_fail 'missing baseline key' 'FAIL baseline.required_keys' \
    "$VERIFIER" --repo "$OUTER" --baseline "$MALFORMED" --expected "$EXPECTED" --allow-nonproduction
printf 'target_fps=30.04\n' >>"$MALFORMED"
printf 'target_fps=30.04\n' >>"$MALFORMED"
run_fail 'duplicate baseline key' 'FAIL baseline.syntax' \
    "$VERIFIER" --repo "$OUTER" --baseline "$MALFORMED" --expected "$EXPECTED" --allow-nonproduction

make_fixture unknown-key
printf 'unknown_key=value\n' >>"$BASELINE"
run_fail 'unknown baseline key' 'FAIL baseline.syntax' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED" --allow-nonproduction

make_fixture malformed-assignment
printf 'target_fps=30.04 extra\n' >>"$BASELINE"
run_fail 'malformed baseline assignment' 'FAIL baseline.syntax' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED" --allow-nonproduction

make_fixture malformed-value
awk -F= '{ if ($1 == "target_fps") print "target_fps=thirty"; else print }' "$BASELINE" >"$BASELINE.tmp"
mv "$BASELINE.tmp" "$BASELINE"
run_fail 'malformed baseline value' 'FAIL baseline.target_fps' \
    "$VERIFIER" --repo "$OUTER" --baseline "$BASELINE" --expected "$EXPECTED" --allow-nonproduction

printf 'all verifier fixture tests passed\n'
