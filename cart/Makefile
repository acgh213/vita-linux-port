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
DEFAULT_CAPTURE_SRC := $(BUILD_DIR)/capture-host.c
DEFAULT_CAPTURE_BIN := $(BUILD_DIR)/capture-host

HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
HOST_LDFLAGS := -fsanitize=address,undefined
TARGET_CFLAGS := -O3 -static -pthread -Wall -Wextra -Werror -march=armv7-a -mfpu=neon -mfloat-abi=hard
TARGET_BIN := $(BUILD_DIR)/pstv-demo-cart.arm

.PHONY: all host-sanitize host-capture capture cross-build cross-verify test complete-test clean FORCE

FORCE:

all: host-sanitize

host-sanitize: $(HOST_BIN)

$(HOST_BIN): $(SRC) FORCE
	@mkdir -p "$(BUILD_DIR)"
	"$(CC)" $(HOST_CFLAGS) "$(SRC)" $(HOST_LDFLAGS) -pthread -lm -o "$@"

ifeq ($(CAPTURE_SRC),$(DEFAULT_CAPTURE_SRC))
$(CAPTURE_SRC): $(SRC) FORCE
	@mkdir -p "$(BUILD_DIR)"
	@printf '%s\n' '#define main pstv_demo_cart_production_main' '#include "../src/pstv-demo-cart.c"' '#undef main' '#include <stdint.h>' '#include <stdio.h>' '#include <stdlib.h>' '' 'static int dump_logical(const char *path)' '{' '    FILE *f = fopen(path, "wb");' '    if (!f) { perror(path); return 1; }' '    if (fprintf(f, "P6\n%d %d\n255\n", LW, LH) < 0) { fclose(f); return 1; }' '    for (int y = 0; y < LH; y++)' '        for (int x = 0; x < LW; x++) {' '            uint32_t c = low[y * LW + x];' '            uint8_t p[3] = { c & 255, (c >> 8) & 255, (c >> 16) & 255 };' '            if (fwrite(p, 1, sizeof(p), f) != sizeof(p)) { fclose(f); return 1; }' '        }' '    return fclose(f) != 0;' '}' 'int main(int argc, char **argv)' '{' '    if (argc != 4) { fprintf(stderr, "usage: %s SCENE FRAME OUTPUT\n", argv[0]); return 2; }' '    for (int i = 0; i < 2048; i++) sins[i] = sinf((float)i * 6.28318530718f / 2048.0f);' '    render_frame(atoi(argv[1]) % SCENES, atoi(argv[2]));' '    return dump_logical(argv[3]);' '}' > "$@"

$(CAPTURE_BIN): $(CAPTURE_SRC) FORCE
	"$(CC)" $(HOST_CFLAGS) "$(CAPTURE_SRC)" $(HOST_LDFLAGS) -pthread -lm -o "$@"
endif

host-capture: $(CAPTURE_BIN)
	@set -eu; \
	mkdir -p "$(BUILD_DIR)"; \
	if [ -e "$(GENERATED_FIXTURES)" ] && [ ! -L "$(GENERATED_FIXTURES)" ]; then \
		printf '%s\n' 'FAIL build/fixtures must be a symlink; remove the legacy real directory before capturing' >&2; \
		exit 1; \
	fi; \
	command -v sha256sum >/dev/null 2>&1 || { printf '%s\n' 'FAIL host-capture: GNU sha256sum is required' >&2; exit 1; }; \
	if ! readlink_version=$$(readlink --version 2>&1); then \
		printf '%s\n' 'FAIL host-capture: GNU coreutils readlink is required' >&2; \
		exit 1; \
	fi; \
	case "$$readlink_version" in \
		*'GNU coreutils'*) ;; \
		*) printf '%s\n' 'FAIL host-capture: GNU coreutils readlink is required' >&2; exit 1 ;; \
	esac; \
	command -v mv >/dev/null 2>&1 || { printf '%s\n' 'FAIL host-capture: GNU mv with -T is required for atomic symlink publication' >&2; exit 1; }; \
	if ! mv --help 2>&1 | grep -q -- '-T'; then \
		printf '%s\n' 'FAIL host-capture: GNU mv with -T is required for atomic symlink publication' >&2; \
		exit 1; \
	fi; \
	stage=; \
	link=; \
	published=0; \
	old=; \
	build_real=; \
	cleanup() { \
		status=$$?; \
		trap - 0 HUP INT TERM; \
		if [ -n "$$link" ]; then rm -f -- "$$link" || status=1; fi; \
		if [ "$$published" -eq 0 ] && [ -n "$$stage" ]; then \
			rm -rf -- "$$stage" || status=1; \
		fi; \
		if [ "$$published" -eq 1 ] && [ -n "$$old" ]; then \
			case "$$old" in \
				/*) old_path=$$old ;; \
				*) old_path="$$build_real/$${old#./}" ;; \
			 esac; \
			if old_real=$$(CDPATH='' cd -- "$$old_path" && pwd -P); then \
				old_parent=$$(CDPATH='' cd -- "$$(dirname -- "$$old_real")" && pwd -P) || old_parent=; \
				old_base=$$(basename -- "$$old_real"); \
				if [ "$$old_parent" = "$$build_real" ]; then \
					case "$$old_base" in \
						.fixtures.*) rm -rf -- "$$old_real" || status=1 ;; \
					esac; \
				fi; \
			fi; \
		fi; \
		exit "$$status"; \
	}; \
	trap cleanup 0; \
	trap 'exit 129' HUP; \
	trap 'exit 130' INT; \
	trap 'exit 143' TERM; \
	build_real=$$(CDPATH='' cd -- "$(BUILD_DIR)" && pwd -P) || { printf '%s\n' 'FAIL host-capture: cannot canonicalize BUILD_DIR' >&2; exit 1; }; \
	if [ -L "$(GENERATED_FIXTURES)" ]; then \
		old=$$(readlink "$(GENERATED_FIXTURES)"); \
		case "$$old" in \
			/*) old_path=$$old ;; \
			*) old_path="$$build_real/$${old#./}" ;; \
		esac; \
		if ! old_real=$$(CDPATH='' cd -- "$$old_path" && pwd -P); then \
			printf '%s\n' 'FAIL host-capture: existing build/fixtures symlink target must resolve directly below build as .fixtures.*' >&2; \
			exit 1; \
		fi; \
		old_parent=$$(CDPATH='' cd -- "$$(dirname -- "$$old_real")" && pwd -P) || { printf '%s\n' 'FAIL host-capture: existing build/fixtures symlink target must resolve directly below build as .fixtures.*' >&2; exit 1; }; \
		old_base=$$(basename -- "$$old_real"); \
		if [ "$$old_parent" != "$$build_real" ]; then \
			printf '%s\n' 'FAIL host-capture: existing build/fixtures symlink target must resolve directly below build as .fixtures.*' >&2; \
			exit 1; \
		fi; \
		case "$$old_base" in \
			.fixtures.*|.fixtures-*) ;; \
			*) printf '%s\n' 'FAIL host-capture: existing build/fixtures symlink target must resolve directly below build as .fixtures.*' >&2; exit 1 ;; \
		esac; \
	fi; \
	stage=$$(mktemp -d "$(BUILD_DIR)/.fixtures.XXXXXX"); \
	stage=$$(CDPATH='' cd -- "$$stage" && pwd -P); \
	link=$$(mktemp "$(BUILD_DIR)/.fixtures-link.XXXXXX"); \
	rm -f -- "$$link"; \
	for scene in 0 1 2 3 4 5; do \
		"$(CAPTURE_BIN)" "$$scene" 120 "$$stage/scene-$$scene-frame-120.ppm"; \
	done; \
	{ \
		printf '%s\n' 'manifest_version=1' 'logical_width=320' 'logical_height=180' 'capture_width=320' 'capture_height=180' 'scene_count=6' 'frame=120'; \
		for scene in 0 1 2 3 4 5; do \
			if ! checksum_output=$$(sha256sum "$$stage/scene-$$scene-frame-120.ppm"); then \
				printf 'FAIL host-capture: sha256sum failed for scene %s\n' "$$scene" >&2; \
				exit 1; \
			fi; \
			checksum=$${checksum_output%% *}; \
			case "$$checksum" in \
				''|*[!0123456789abcdefABCDEF]*) printf 'FAIL host-capture: invalid sha256sum output for scene %s\n' "$$scene" >&2; exit 1 ;; \
			 esac; \
			if [ "$${#checksum}" -ne 64 ]; then \
				printf 'FAIL host-capture: invalid sha256sum output for scene %s\n' "$$scene" >&2; \
				exit 1; \
			fi; \
			printf 'scene_%s_sha256=%s\n' "$$scene" "$$checksum"; \
		done; \
	} > "$$stage/manifest.txt.tmp"; \
	mv "$$stage/manifest.txt.tmp" "$$stage/manifest.txt"; \
	for scene in 0 1 2 3 4 5; do \
		test -s "$$stage/scene-$$scene-frame-120.ppm"; \
	done; \
	test -s "$$stage/manifest.txt"; \
	ln -s "$$stage" "$$link"; \
	if mv -T "$$link" "$(GENERATED_FIXTURES)"; then \
		:; \
	else \
		status=$$?; \
		exit "$$status"; \
	fi; \
	published=1; exit 0

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
	"$(SHELL)" "$(CART_DIR)/tests/test_legacy_render.sh" --verify-only --compare-to "$(GENERATED_FIXTURES)"
	@if [ "$${CART_IMMUTABILITY_GUARD:-}" != child ] && [ -z "$${CART_SKIP_IMMUTABILITY:-}" ]; then \
		CART_IMMUTABILITY_GUARD=child CART_SKIP_IMMUTABILITY=1 \
		"$(SHELL)" "$(CART_DIR)/tests/test_fixture_immutability.sh"; \
	fi
	$(MAKE) cross-verify

complete-test: test
	"$(SHELL)" "$(CART_DIR)/tests/test_make_harness.sh"
	"$(SHELL)" "$(CART_DIR)/tests/test_verify_baseline.sh"
	"$(SHELL)" "$(CART_DIR)/tests/test_fb_ownership.sh"
	"$(SHELL)" "$(CART_DIR)/scripts/verify-baseline.sh" --repo "$(abspath $(CART_DIR)/..)" --arm-binary "$(TARGET_BIN)"

clean:
	@set -eu; \
	cart_real=$$(CDPATH='' cd -- "$(CART_DIR)" && pwd -P) || { printf '%s\n' 'FAIL clean: cannot canonicalize cart directory' >&2; exit 1; }; \
	build_input="$(BUILD_DIR)"; \
	if [ -d "$$build_input" ]; then \
		build_real=$$(CDPATH='' cd -- "$$build_input" && pwd -P) || { printf '%s\n' 'FAIL clean: cannot canonicalize BUILD_DIR' >&2; exit 1; }; \
	else \
		build_parent=$$(CDPATH='' cd -- "$$(dirname -- "$$build_input")" && pwd -P) || { printf '%s\n' 'FAIL clean: cannot canonicalize BUILD_DIR parent' >&2; exit 1; }; \
		build_base=$$(basename -- "$$build_input"); \
		[ -n "$$build_base" ] || { printf '%s\n' 'FAIL clean: BUILD_DIR must name a directory below the cart' >&2; exit 1; }; \
		build_real="$$build_parent/$$build_base"; \
	fi; \
	case "$$build_real/" in \
		"$$cart_real"/*) ;; \
		*) printf '%s\n' 'FAIL clean: BUILD_DIR must resolve below the cart directory' >&2; exit 1 ;; \
	esac; \
	[ "$$build_real" != "$$cart_real" ] || { printf '%s\n' 'FAIL clean: BUILD_DIR must not resolve to the cart directory' >&2; exit 1; }; \
	rm -rf -- "$$build_input"
