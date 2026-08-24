SHELL := /bin/sh

CC ?= cc
CROSS_CC ?= arm-linux-gnueabihf-gcc
CART_DIR := .
SRC := $(CART_DIR)/src/pstv-demo-cart.c
BUILD_DIR := $(CART_DIR)/build
HOST_BIN := $(BUILD_DIR)/pstv-demo-cart.host-sanitize
CAPTURE_SRC := $(BUILD_DIR)/capture-host.c
CAPTURE_BIN := $(BUILD_DIR)/capture-host
FIXTURES := $(CART_DIR)/tests/fixtures
MANIFEST := $(FIXTURES)/manifest.txt
GENERATED_FIXTURES := $(BUILD_DIR)/fixtures
GENERATED_MANIFEST := $(GENERATED_FIXTURES)/manifest.txt

HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
HOST_LDFLAGS := -fsanitize=address,undefined
TARGET_CFLAGS := -O3 -static -pthread -Wall -Wextra -Werror -march=armv7-a -mfpu=neon -mfloat-abi=hard
TARGET_BIN := $(BUILD_DIR)/pstv-demo-cart.arm

.PHONY: all host-sanitize host-capture capture cross-build cross-verify test clean

all: host-sanitize

host-sanitize: $(HOST_BIN)

$(HOST_BIN): $(SRC)
	@mkdir -p "$(BUILD_DIR)"
	"$(CC)" $(HOST_CFLAGS) "$(SRC)" $(HOST_LDFLAGS) -pthread -lm -o "$@"

$(CAPTURE_SRC): $(SRC)
	@mkdir -p "$(BUILD_DIR)"
	@printf '%s\n' '#define main pstv_demo_cart_production_main' '#include "../src/pstv-demo-cart.c"' '#undef main' '#include <stdint.h>' '#include <stdio.h>' '#include <stdlib.h>' '' 'static int dump_logical(const char *path)' '{' '    FILE *f = fopen(path, "wb");' '    if (!f) { perror(path); return 1; }' '    if (fprintf(f, "P6\\n%d %d\\n255\\n", LW, LH) < 0) { fclose(f); return 1; }' '    for (int y = 0; y < LH; y++)' '        for (int x = 0; x < LW; x++) {' '            uint32_t c = low[y * LW + x];' '            uint8_t p[3] = { c & 255, (c >> 8) & 255, (c >> 16) & 255 };' '            if (fwrite(p, 1, sizeof(p), f) != sizeof(p)) { fclose(f); return 1; }' '        }' '    return fclose(f) != 0;' '}' 'int main(int argc, char **argv)' '{' '    if (argc != 4) { fprintf(stderr, "usage: %s SCENE FRAME OUTPUT\\n", argv[0]); return 2; }' '    for (int i = 0; i < 2048; i++) sins[i] = sinf((float)i * 6.28318530718f / 2048.0f);' '    render_frame(atoi(argv[1]) % SCENES, atoi(argv[2]));' '    return dump_logical(argv[3]);' '}' > "$@"

$(CAPTURE_BIN): $(CAPTURE_SRC)
	"$(CC)" $(HOST_CFLAGS) "$(CAPTURE_SRC)" $(HOST_LDFLAGS) -pthread -lm -o "$@"

host-capture: $(CAPTURE_BIN)
	@set -eu; \
	mkdir -p "$(BUILD_DIR)"; \
	stage=$$(mktemp -d "$(BUILD_DIR)/.fixtures.XXXXXX"); \
	old=; \
	cleanup() { \
		status=$$?; \
		trap - 0 HUP INT TERM; \
		rm -rf "$$stage"; \
		if [ -n "$$old" ] && [ ! -e "$(GENERATED_FIXTURES)" ] && [ ! -L "$(GENERATED_FIXTURES)" ] && [ -e "$$old" ]; then \
			mv "$$old" "$(GENERATED_FIXTURES)" || status=1; \
		fi; \
		exit "$$status"; \
	}; \
	trap cleanup 0 HUP INT TERM; \
	for scene in 0 1 2 3 4 5; do \
		"$(CAPTURE_BIN)" "$$scene" 120 "$$stage/scene-$$scene-frame-120.ppm"; \
	done; \
	{ \
		printf '%s\n' 'manifest_version=1' 'logical_width=320' 'logical_height=180' 'capture_width=320' 'capture_height=180' 'scene_count=6' 'frame=120'; \
		for scene in 0 1 2 3 4 5; do \
			printf 'scene_%s_sha256=%s\n' "$$scene" "$$(sha256sum "$$stage/scene-$$scene-frame-120.ppm" | awk '{print $$1}')"; \
		done; \
	} > "$$stage/manifest.txt.tmp"; \
	mv "$$stage/manifest.txt.tmp" "$$stage/manifest.txt"; \
	for scene in 0 1 2 3 4 5; do \
		test -s "$$stage/scene-$$scene-frame-120.ppm"; \
	done; \
	test -s "$$stage/manifest.txt"; \
	if [ -e "$(GENERATED_FIXTURES)" ] || [ -L "$(GENERATED_FIXTURES)" ]; then \
		old="$(GENERATED_FIXTURES).old.$$"; \
		mv "$(GENERATED_FIXTURES)" "$$old"; \
	fi; \
	if ! mv "$$stage" "$(GENERATED_FIXTURES)"; then \
		status=$$?; \
		if [ -n "$$old" ] && [ -e "$$old" ]; then mv "$$old" "$(GENERATED_FIXTURES)" || status=1; fi; \
		exit "$$status"; \
	fi; \
	if [ -n "$$old" ]; then rm -rf "$$old"; fi; \
	old=; \
	trap - 0 HUP INT TERM

capture: host-capture

cross-build:
	@mkdir -p "$(BUILD_DIR)"
	@command -v "$(CROSS_CC)" >/dev/null 2>&1 || { printf 'FAIL cross-build: %s not found\n' "$(CROSS_CC)" >&2; exit 1; }
	"$(CROSS_CC)" $(TARGET_CFLAGS) "$(SRC)" -lm -o "$(TARGET_BIN)"

cross-verify: cross-build
	@command -v file >/dev/null 2>&1 || { printf '%s\n' 'FAIL cross-verify: file not found' >&2; exit 1; }
	@command -v readelf >/dev/null 2>&1 || { printf '%s\n' 'FAIL cross-verify: readelf not found' >&2; exit 1; }
	@description=$$(file -b "$(TARGET_BIN)"); case "$$description" in *ARM*EABI5*'statically linked'*) ;; *) printf 'FAIL cross-verify: %s\n' "$$description" >&2; exit 1 ;; esac
	@readelf -h "$(TARGET_BIN)" | grep -q 'Flags:.*hard-float ABI'
	@if readelf -l "$(TARGET_BIN)" | grep -q 'INTERP'; then printf '%s\n' 'FAIL cross-verify: dynamic interpreter present' >&2; exit 1; fi

test: host-sanitize host-capture
	"$(SHELL)" "$(CART_DIR)/tests/test_legacy_render.sh" --verify-only
	@if [ -z "$${CART_SKIP_IMMUTABILITY:-}" ]; then \
		"$(SHELL)" "$(CART_DIR)/tests/test_fixture_immutability.sh"; \
	fi
	$(MAKE) cross-verify

clean:
	rm -rf "$(BUILD_DIR)"
