#!/bin/sh
# Self-test for scripts/capture-flash-check.py.
#
# Generates known-bad and known-good frame sequences from the REAL cart
# renderer, encodes them to H.264, and asserts the checker's verdict and exit
# code on each. This is what makes the hardware re-validation protocol
# trustworthy: if the checker cannot tell a strobe from a clean crossfade on
# synthetic video, its verdict on a real capture means nothing.
#
# Skips cleanly if ffmpeg is unavailable.

set -eu

CART_DIR=${1:-.}
CHECKER="$CART_DIR/scripts/capture-flash-check.py"
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT INT TERM

fail() {
    printf 'not ok - %s\n' "$1" >&2
    exit 1
}

pass() {
    printf 'ok - %s\n' "$1"
}

[ -f "$CHECKER" ] || fail 'capture-flash-check.py is missing'

if ! command -v ffmpeg >/dev/null 2>&1; then
    printf 'ok - capture flash checker self-test skipped (ffmpeg unavailable)\n'
    exit 0
fi
if ! command -v python3 >/dev/null 2>&1; then
    printf 'ok - capture flash checker self-test skipped (python3 unavailable)\n'
    exit 0
fi

# Build the sequence generator against the real scene and transition sources.
GEN_SRC="$TMP_ROOT/genseq.c"
cat >"$GEN_SRC" <<'GENEOF'
#define _GNU_SOURCE
#include <cart/canvas.h>
#include <cart/scene.h>
#include <cart/transition.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LW 320
#define LH 180
static uint32_t low[LW * LH];
static uint32_t low_from[LW * LH];
static uint32_t low_to[LW * LH];
static void render_into(uint32_t *destination, size_t which, uint64_t frame)
{
    struct cart_canvas canvas = { .pixels = destination, .width = LW,
                                  .height = LH, .stride = LW };
    const struct cart_scene *scene = cart_scene_at(which);
    struct cart_scene_render_context context = {
        .canvas = &canvas, .row_start = 0, .row_end = LH,
        .frame = frame, .phase = CART_SCENE_RENDER_ROWS };
    scene->render(&context);
    context.phase = CART_SCENE_RENDER_OVERLAY;
    scene->render(&context);
}
static int write_ppm(const char *directory, int index, const uint32_t *pixels)
{
    char path[512];
    FILE *file;
    snprintf(path, sizeof(path), "%s/frame-%04d.ppm", directory, index);
    file = fopen(path, "wb");
    if (file == NULL) { perror(path); return 1; }
    fprintf(file, "P6\n%d %d\n255\n", LW, LH);
    for (int i = 0; i < LW * LH; i++) {
        uint32_t c = pixels[i];
        uint8_t rgb[3] = { c & 255, (c >> 8) & 255, (c >> 16) & 255 };
        fwrite(rgb, 1, sizeof(rgb), file);
    }
    return fclose(file) != 0;
}
int main(int argc, char **argv)
{
    int emitted = 0;
    if (argc != 3) return 2;
    if (cart_scenes_init() != 0) return 1;
    if (strcmp(argv[1], "strobe") == 0) {
        for (int i = 0; i < 60; i++) {
            render_into(low, (size_t)(i % 6), 240 + (uint64_t)i);
            if (write_ppm(argv[2], emitted++, low)) return 1;
        }
    } else if (strcmp(argv[1], "crossfade") == 0) {
        for (int cut = 0; cut < 4; cut++) {
            size_t from = (size_t)(cut % 6), to = (size_t)((cut + 1) % 6);
            struct cart_transition transition;
            for (int hold = 0; hold < 10; hold++) {
                render_into(low, from, 240 + (uint64_t)hold);
                if (write_ppm(argv[2], emitted++, low)) return 1;
            }
            cart_transition_begin(&transition, from, to);
            for (uint64_t step = 0; step <= CART_TRANSITION_FRAMES; step++) {
                render_into(low_from, from, 250 + step);
                render_into(low_to, to, 250 + step);
                cart_transition_blend(&transition, low, low_from, low_to,
                                      (size_t)(LW * LH), step);
                if (write_ppm(argv[2], emitted++, low)) return 1;
            }
        }
    } else return 2;
    return 0;
}
GENEOF

CC=${CC:-cc}
"$CC" -std=c11 -O2 -I"$CART_DIR/include" -o "$TMP_ROOT/genseq" "$GEN_SRC" \
    "$CART_DIR/src/canvas.c" "$CART_DIR/src/transition.c" \
    "$CART_DIR/src/scenes_builtin.c" "$CART_DIR/scenes/scene_2d_legacy.c" \
    -lm 2>/dev/null || fail 'could not build the sequence generator'

encode() {
    mode=$1
    mkdir -p "$TMP_ROOT/seq-$mode"
    "$TMP_ROOT/genseq" "$mode" "$TMP_ROOT/seq-$mode" \
        || fail "sequence generation failed for $mode"
    ffmpeg -v error -y -framerate 30 -i "$TMP_ROOT/seq-$mode/frame-%04d.ppm" \
        -c:v libx264 -pix_fmt yuv420p -crf 14 "$TMP_ROOT/$mode.mp4" \
        >/dev/null 2>&1 || fail "ffmpeg encode failed for $mode"
}

encode strobe
encode crossfade

# A per-frame scene change must be reported as a rate violation (exit 2).
set +e
python3 "$CHECKER" "$TMP_ROOT/strobe.mp4" --grid 2 >"$TMP_ROOT/strobe.out" 2>&1
STROBE_STATUS=$?
set -e
[ "$STROBE_STATUS" -eq 2 ] || {
    cat "$TMP_ROOT/strobe.out" >&2
    fail "strobe capture must exit 2, got $STROBE_STATUS"
}
grep -q "RATE EXCEEDED" "$TMP_ROOT/strobe.out" \
    || fail 'strobe capture must report a rate violation'
pass 'capture checker flags a per-frame scene strobe'

# The repaired crossfade must survive H.264 encode/decode and pass (exit 0).
set +e
python3 "$CHECKER" "$TMP_ROOT/crossfade.mp4" --grid 2 \
    >"$TMP_ROOT/crossfade.out" 2>&1
FADE_STATUS=$?
set -e
[ "$FADE_STATUS" -eq 0 ] || {
    cat "$TMP_ROOT/crossfade.out" >&2
    fail "crossfade capture must exit 0, got $FADE_STATUS"
}
grep -q "VERDICT: PASS" "$TMP_ROOT/crossfade.out" \
    || fail 'crossfade capture must pass'
pass 'capture checker passes the repaired crossfade through H.264'

# A localized strobe must be caught by the grid even when the whole-frame
# average stays under threshold. Without this the checker would sign off on a
# real hazard.
mkdir -p "$TMP_ROOT/seq-local"
python3 - "$TMP_ROOT/seq-local" <<'PYEOF'
import sys
directory = sys.argv[1]
width, height = 320, 180
for index in range(60):
    bright = 255 if index % 2 == 0 else 0
    row_hot = bytes((bright, bright, bright)) * (width // 4)
    row_cold = bytes((90, 90, 90)) * (width - width // 4)
    top = (row_hot + row_cold) * (height // 4)
    rest = (bytes((90, 90, 90)) * width) * (height - height // 4)
    header = b"P6\n%d %d\n255\n" % (width, height)
    with open(f"{directory}/frame-{index:04d}.ppm", "wb") as handle:
        handle.write(header + top + rest)
PYEOF
ffmpeg -v error -y -framerate 30 -i "$TMP_ROOT/seq-local/frame-%04d.ppm" \
    -c:v libx264 -pix_fmt yuv420p -crf 12 "$TMP_ROOT/local.mp4" \
    >/dev/null 2>&1 || fail 'ffmpeg encode failed for the localized case'

set +e
python3 "$CHECKER" "$TMP_ROOT/local.mp4" --grid 0 >"$TMP_ROOT/local-global.out" 2>&1
LOCAL_GLOBAL_STATUS=$?
python3 "$CHECKER" "$TMP_ROOT/local.mp4" --grid 4 >"$TMP_ROOT/local-grid.out" 2>&1
LOCAL_GRID_STATUS=$?
set -e
[ "$LOCAL_GLOBAL_STATUS" -eq 0 ] \
    || fail 'localized-strobe fixture no longer hides from the global average'
[ "$LOCAL_GRID_STATUS" -eq 2 ] || {
    cat "$TMP_ROOT/local-grid.out" >&2
    fail "localized strobe must be caught by the grid, got $LOCAL_GRID_STATUS"
}
pass 'capture checker grid catches a localized strobe the global average misses'

printf 'all capture flash checker tests passed\n'
