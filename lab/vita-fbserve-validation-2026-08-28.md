# `vita-fbserve` validation — 2026-08-28

## Scope

B7 adds a read-only HTTP framebuffer snapshot utility to the native Vita
Toolkit payload. It reads `/dev/fb0`, reduces the fixed PSTV 1280×720 RGBA frame
to a 320×180 24-bit BMP, and never writes the framebuffer or fbcon state.

The default listener is `127.0.0.1`. LAN exposure requires the explicit
`--bind 0.0.0.0` option. The server accepts `GET /`, `GET /frame.bmp`, and
`GET /health`; non-GET requests receive `405`.

## Host gates

- `toolkit/tests/test-vita-fbserve.sh` passed.
- All toolkit shell tests passed.
- `shellcheck` passed for the changed shell scripts.
- `sh -n` passed for the changed shell scripts.
- `make test` passed, including the cart complete-test and ARM cross-build.
- The staging builder passed with `MKSQUASHFS=definitely-missing`.
- Staged `README.md` is byte-identical to the committed toolkit README.
- Staged `MANIFEST` includes the wrapper, ARM binary, and C source.

Final static ARM artifact:

```text
file: toolkit/bin/vita-fbserve.arm
ELF 32-bit LSB executable, ARM, EABI5, statically linked
sha256: c297d775469ab4c17d8be174c66d8f0619aa6e9dc4e67bd7525e38fa3804542d
```

The SquashFS filesystem creation step is an external handoff because Hermes'
safety layer blocks `mksquashfs`; the verified image was created outside Hermes
and then deployed to the removable payload store.

Persistent image:

```text
path: /mnt/vita-storage/vita-toolkit-native-b7.squashfs
host sha256: c3d182ad808fa933a3a3503ca939e4aedd93a5cd6f4acf61059dc6d57a6bab1e
bytes: 10383360
```

## PSTV preflight

Target: `192.168.18.43`

```text
preflight=pass
known-good cart: /usr/local/bin/pstv-demo-cart
online=0-3
fbcon=0
candidate absent before copy
```

Only `/tmp/vita-fbserve.arm` was copied to the target. The known-good cart and
rootfs were not modified.

## Localhost smoke test

The candidate ran directly from `/tmp` with its default bind address:

```text
schema=1
bind=127.0.0.1
read_only=1
```

A target-local `wget` request to `http://127.0.0.1:18080/frame.bmp` returned a
complete 172,854-byte BMP. The test also exercised `/health` and confirmed the
`405 Method Not Allowed` response for `POST`.

## LAN smoke test

The candidate was then restarted with the deliberate LAN opt-in:

```text
/tmp/vita-fbserve.arm --machine --bind 0.0.0.0 --port 18081
```

This Debian server fetched:

```text
http://192.168.18.43:18081/frame.bmp
```

The response was a valid complete image:

```text
PC bitmap, Windows 3.x format, 320 x 180 x 24
image size 172800
cbSize 172854
bits offset 54
```

No display changes were requested; the utility was read-only throughout.

## Persistent-payload runtime test

The image was mounted from the removable partition with an explicit
`mount -t squashfs -o loop,ro,nosuid,nodev` operation at
`/opt/vita-toolkit-b7`. The target did not have the pre-existing
`vita-toolkit-mount` wrapper installed, so the equivalent direct read-only
mount was used for this deployment check.

`/opt/vita-toolkit-b7/bin/vita-fbserve` launched the embedded
`libexec/vita-fbserve.arm` successfully. Both a target-local request and a LAN
request from this Debian server returned complete 172,854-byte BMPs.

## Postflight

```text
cart_pid=28289
cart_exe=/usr/local/bin/pstv-demo-cart
online=0-3
fbcon=0
candidate_binary=absent
candidate_processes=0
fault_scan=clean
```

The PSTV was left running the embedded known-good cart. The versioned B7 image
remains on the removable exFAT partition; the exFAT and loop mounts were
removed after the runtime test.

## Verdict

**PASS.** The utility is host-tested, statically cross-built for ARM, included
in the staged toolkit payload, safe by default on localhost, explicitly usable
from the LAN, and cleanly removed after hardware validation.
