#!/bin/sh
# Verify the cart production provenance baseline.
set -u

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P) || exit 1
DEFAULT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd -P) || exit 1

PRODUCTION_OUTER_COMMIT=c071137708a0b157deaae996f7ea01dc74b9c92e
PRODUCTION_KERNEL_GITLINK=996f030022f021b19e5c9182fca5cc1c60300969
PRODUCTION_LOADER_GITLINK=435791fd6aa70d2458f48be893358f106be0234d
PRODUCTION_SOURCE_C_SHA256=22f4dc5f65a294f01fbb6f96fb99e624e7a7cd86dcd808c7da5dcc1a589240f1
PRODUCTION_SOURCE_START_SHA256=5bf93b22524f16ae95c87c64f7a76443fd4423da64059850b06fcccbbf479a14
PRODUCTION_SOURCE_STOP_SHA256=0daae2bf622b4c07f1509a541f8729ace261dfcd7960605ee917fc935e7f28c0
PRODUCTION_SOURCE_README_SHA256=55f4acb385d4aa047178401d64bdc9a3421829406d40bfc58fbeb9a067d002ab
PRODUCTION_DEMO_BINARY_SHA256=4eee4b0676a546b1d576af8a7f3f97a7ad01d6e9db95a238b3a9ac785fadae0b
PRODUCTION_TARGET_MODEL=pstv
PRODUCTION_TARGET_FRAMEBUFFER=1280x720
PRODUCTION_TARGET_LOGICAL_RENDER=320x180
PRODUCTION_TARGET_FPS=30.04
PRODUCTION_TINYGL_COMMIT=c2e48591a6bfba1a85f1b87a78dcf1abf7dff57a

REQUIRED_KEYS='schema_version production_outer_commit production_kernel_gitlink production_loader_gitlink known_good_pstv_demo_cart_c_sha256 known_good_start_demo_cart_sh_sha256 known_good_stop_demo_cart_sh_sha256 known_good_readme_md_sha256 known_good_demo_binary_sha256 target_model target_framebuffer target_logical_render target_fps tinygl_tested_commit'

repo=${VITA_REPO_ROOT:-$DEFAULT_ROOT}
baseline=
expected=
allow_nonproduction=0

fail() {
    printf 'FAIL %s\n' "$1" >&2
    exit 1
}

usage() {
    printf '%s\n' 'Usage: verify-baseline.sh [--repo PATH] [--baseline PATH] [--expected PATH --allow-nonproduction] [--allow-nonproduction]' >&2
    exit 2
}

# --expected is an isolated-fixture hook; it is rejected without the explicit
# --allow-nonproduction override and is never consulted in production mode.
while [ "$#" -gt 0 ]; do
    case $1 in
        --repo)
            [ "$#" -ge 2 ] || usage
            repo=$2
            shift 2
            ;;
        --baseline)
            [ "$#" -ge 2 ] || usage
            baseline=$2
            shift 2
            ;;
        --expected)
            [ "$#" -ge 2 ] || usage
            expected=$2
            shift 2
            ;;
        --allow-nonproduction)
            allow_nonproduction=1
            shift
            ;;
        --help|-h)
            usage
            ;;
        *)
            usage
            ;;
    esac
done

if [ -n "$expected" ] && [ "$allow_nonproduction" -ne 1 ]; then
    fail 'expected constants require --allow-nonproduction'
fi
if [ "$allow_nonproduction" -eq 1 ] && [ -z "$expected" ]; then
    fail '--allow-nonproduction requires --expected PATH'
fi

if ! repo=$(CDPATH='' cd -- "$repo" && pwd -P); then
    fail 'repository.path'
fi
if [ -z "$baseline" ]; then
    baseline=$repo/cart/provenance/production-baseline.txt
fi

if [ "$allow_nonproduction" -eq 1 ]; then
    printf 'NOTICE nonproduction_override (explicit test fixture mode)\n'
fi

# Validate syntax, duplicate keys, and the complete known schema without eval.
validate_file() {
    file=$1
    label=$2
    [ -f "$file" ] || fail "$label.exists"
    if ! awk '
        /^[[:space:]]*$/ || /^[[:space:]]*#/ { next }
        !/^[a-z][a-z0-9_]*=[^[:space:]]+$/ { bad=1; next }
        { key=$0; sub(/=.*/, "", key); if (seen[key]++) { duplicate=1 } }
        END { if (bad || duplicate) exit 1 }
    ' "$file"; then
        fail "$label.syntax"
    fi
    for key in $REQUIRED_KEYS; do
        value=$(awk -F= -v wanted="$key" '$1 == wanted { print substr($0, index($0, "=") + 1) }' "$file")
        [ -n "$value" ] || fail "$label.required_keys"
    done
    unknown=$(awk -F= '
        /^[[:space:]]*$/ || /^[[:space:]]*#/ { next }
        { print $1 }
    ' "$file" | while IFS= read -r key; do
        case " $REQUIRED_KEYS " in
            *" $key "*) ;;
            *) printf '%s\n' "$key"; break ;;
        esac
    done)
    [ -z "$unknown" ] || fail "$label.syntax"
}

value_from() {
    file=$1
    key=$2
    awk -F= -v wanted="$key" '$1 == wanted { print substr($0, index($0, "=") + 1) }' "$file"
}

is_hex() {
    value=$1
    length=$2
    printf '%s\n' "$value" | awk -v expected_length="$length" \
        'length($0) == expected_length && $0 !~ /[^0-9a-f]/ { exit 0 } { exit 1 }'
}

validate_values() {
    file=$1
    label=$2
    schema=$(value_from "$file" schema_version)
    outer=$(value_from "$file" production_outer_commit)
    kernel=$(value_from "$file" production_kernel_gitlink)
    loader=$(value_from "$file" production_loader_gitlink)
    source_c=$(value_from "$file" known_good_pstv_demo_cart_c_sha256)
    source_start=$(value_from "$file" known_good_start_demo_cart_sh_sha256)
    source_stop=$(value_from "$file" known_good_stop_demo_cart_sh_sha256)
    source_readme=$(value_from "$file" known_good_readme_md_sha256)
    binary=$(value_from "$file" known_good_demo_binary_sha256)
    model=$(value_from "$file" target_model)
    framebuffer=$(value_from "$file" target_framebuffer)
    logical_render=$(value_from "$file" target_logical_render)
    fps=$(value_from "$file" target_fps)
    tinygl=$(value_from "$file" tinygl_tested_commit)

    [ "$schema" = 1 ] || fail "$label.schema_version"
    is_hex "$outer" 40 || fail "$label.production_outer_commit"
    is_hex "$kernel" 40 || fail "$label.production_kernel_gitlink"
    is_hex "$loader" 40 || fail "$label.production_loader_gitlink"
    is_hex "$source_c" 64 || fail "$label.known_good_pstv_demo_cart_c_sha256"
    is_hex "$source_start" 64 || fail "$label.known_good_start_demo_cart_sh_sha256"
    is_hex "$source_stop" 64 || fail "$label.known_good_stop_demo_cart_sh_sha256"
    is_hex "$source_readme" 64 || fail "$label.known_good_readme_md_sha256"
    is_hex "$binary" 64 || fail "$label.known_good_demo_binary_sha256"
    is_hex "$tinygl" 40 || fail "$label.tinygl_tested_commit"
    [ "$model" = pstv ] || fail "$label.target_model"
    printf '%s\n' "$framebuffer" | awk '/^[0-9]+x[0-9]+$/ { ok=1 } END { exit !ok }' || fail "$label.target_framebuffer"
    printf '%s\n' "$logical_render" | awk '/^[0-9]+x[0-9]+$/ { ok=1 } END { exit !ok }' || fail "$label.target_logical_render"
    printf '%s\n' "$fps" | awk '/^[0-9]+[.][0-9]+$/ { ok=1 } END { exit !ok }' || fail "$label.target_fps"
}

validate_file "$baseline" baseline
validate_values "$baseline" baseline
printf 'PASS baseline.schema_version\n'

if [ "$allow_nonproduction" -eq 1 ]; then
    validate_file "$expected" expected
    validate_values "$expected" expected
    for key in $REQUIRED_KEYS; do
        baseline_value=$(value_from "$baseline" "$key")
        expected_value=$(value_from "$expected" "$key")
        [ "$baseline_value" = "$expected_value" ] || fail "baseline.$key"
    done
else
    check_production_value() {
        key=$1
        expected_value=$2
        [ "$(value_from "$baseline" "$key")" = "$expected_value" ] || fail "baseline.$key"
    }
    check_production_value schema_version 1
    check_production_value production_outer_commit "$PRODUCTION_OUTER_COMMIT"
    check_production_value production_kernel_gitlink "$PRODUCTION_KERNEL_GITLINK"
    check_production_value production_loader_gitlink "$PRODUCTION_LOADER_GITLINK"
    check_production_value known_good_pstv_demo_cart_c_sha256 "$PRODUCTION_SOURCE_C_SHA256"
    check_production_value known_good_start_demo_cart_sh_sha256 "$PRODUCTION_SOURCE_START_SHA256"
    check_production_value known_good_stop_demo_cart_sh_sha256 "$PRODUCTION_SOURCE_STOP_SHA256"
    check_production_value known_good_readme_md_sha256 "$PRODUCTION_SOURCE_README_SHA256"
    check_production_value known_good_demo_binary_sha256 "$PRODUCTION_DEMO_BINARY_SHA256"
    check_production_value target_model "$PRODUCTION_TARGET_MODEL"
    check_production_value target_framebuffer "$PRODUCTION_TARGET_FRAMEBUFFER"
    check_production_value target_logical_render "$PRODUCTION_TARGET_LOGICAL_RENDER"
    check_production_value target_fps "$PRODUCTION_TARGET_FPS"
    check_production_value tinygl_tested_commit "$PRODUCTION_TINYGL_COMMIT"
fi

production_outer_commit=$(value_from "$baseline" production_outer_commit)
production_kernel_gitlink=$(value_from "$baseline" production_kernel_gitlink)
production_loader_gitlink=$(value_from "$baseline" production_loader_gitlink)

if ! outer_root=$(git -C "$repo" rev-parse --show-toplevel 2>/dev/null); then
    fail 'outer.repository'
fi
if ! outer_root_real=$(CDPATH='' cd -- "$outer_root" && pwd -P); then
    fail 'outer.repository'
fi
[ "$outer_root_real" = "$repo" ] || fail 'outer.repository'
if ! outer_head=$(git -C "$repo" rev-parse --verify 'HEAD^{commit}' 2>/dev/null); then
    fail 'outer.head'
fi
printf 'PASS outer.head\n'

if ! git -C "$repo" cat-file -e "$production_outer_commit^{commit}" 2>/dev/null; then
    fail 'outer.ancestor'
fi
if git -C "$repo" merge-base --is-ancestor "$production_outer_commit" "$outer_head" 2>/dev/null; then
    printf 'PASS outer.ancestor\n'
else
    status=$?
    if [ "$status" -eq 1 ]; then
        fail 'outer.ancestor'
    fi
    fail 'outer.git_command'
fi

check_gitlink() {
    name=$1
    path=$2
    expected_commit=$3
    if ! tree_line=$(git -C "$repo" ls-tree HEAD -- "$path" 2>/dev/null); then
        fail "outer.${name}_git_command"
    fi
    mode=$(printf '%s\n' "$tree_line" | awk '{ print $1 }')
    tree_commit=$(printf '%s\n' "$tree_line" | awk '{ print $3 }')
    [ "$mode" = 160000 ] || fail "outer.${name}_gitlink"
    [ "$tree_commit" = "$expected_commit" ] || fail "outer.${name}_gitlink"
    printf 'PASS outer.%s_gitlink\n' "$name"
}

check_gitlink kernel linux_vita "$production_kernel_gitlink"
check_gitlink loader vita-baremetal-linux-loader "$production_loader_gitlink"

check_submodule() {
    name=$1
    path=$2
    expected_commit=$3
    [ -d "$repo/$path" ] || fail "submodule.${name}_initialized"
    if ! sub_root=$(git -C "$repo/$path" rev-parse --show-toplevel 2>/dev/null); then
        fail "submodule.${name}_initialized"
    fi
    if ! sub_root_real=$(CDPATH='' cd -- "$sub_root" && pwd -P); then
        fail "submodule.${name}_git_command"
    fi
    if ! path_real=$(CDPATH='' cd -- "$repo/$path" && pwd -P); then
        fail "submodule.${name}_git_command"
    fi
    [ "$sub_root_real" = "$path_real" ] || fail "submodule.${name}_initialized"
    if ! sub_head=$(git -C "$repo/$path" rev-parse --verify 'HEAD^{commit}' 2>/dev/null); then
        fail "submodule.${name}_commit"
    fi
    [ "$sub_head" = "$expected_commit" ] || fail "submodule.${name}_commit"
    printf 'PASS submodule.%s_commit\n' "$name"

    if git -C "$repo/$path" diff --quiet -- 2>/dev/null; then
        :
    else
        status=$?
        [ "$status" -eq 1 ] && fail "submodule.${name}_clean"
        fail "submodule.${name}_git_command"
    fi
    if git -C "$repo/$path" diff --cached --quiet -- 2>/dev/null; then
        :
    else
        status=$?
        [ "$status" -eq 1 ] && fail "submodule.${name}_clean"
        fail "submodule.${name}_git_command"
    fi
    if ! sub_status=$(git -C "$repo/$path" status --porcelain --untracked-files=all 2>/dev/null); then
        fail "submodule.${name}_git_command"
    fi
    [ -z "$sub_status" ] || fail "submodule.${name}_clean"
    printf 'PASS submodule.%s_clean\n' "$name"
}

check_submodule kernel linux_vita "$production_kernel_gitlink"
check_submodule loader vita-baremetal-linux-loader "$production_loader_gitlink"
printf 'PASS baseline.production_provenance\n'
