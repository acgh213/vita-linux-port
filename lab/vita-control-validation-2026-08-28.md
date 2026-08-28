# B8 `vita-control` validation — 2026-08-28

## Artifact

The externally-created image was verified before deployment:

```text
path: dist/vita-toolkit-native-b8.squashfs
version: 0.4.0
bytes: 10604544
sha256: e54fad9ae5569fbb3b15848b26c243289f33064353575d4f59c3f36d5d7d4d64
manifest entries: 1530
manifest: byte-for-byte match with a fresh staged manifest
```

The image was created outside Hermes because its safety layer blocks the
`mksquashfs` image-construction command.

## Persistent deployment

The image was copied to the removable exFAT payload store as:

```text
/mnt/vita-storage/vita-toolkit-native-b8.squashfs
```

Target-side verification:

```text
target bytes: 10604544
target sha256: e54fad9ae5569fbb3b15848b26c243289f33064353575d4f59c3f36d5d7d4d64
```

## Runtime test

The payload was mounted from the persistent USB image at
`/opt/vita-toolkit-b8` with:

```text
mount -t squashfs -o loop,ro,nosuid,nodev
```

The `/proc/mounts` entry confirmed `squashfs ro,nosuid,nodev`.

The mounted payload's own wrapper was used with `VITA_CONTROL_ROOT` pointed at
the isolated mount:

```sh
vita-control --pidfile /run/pstv-demo-cart.pid \
  --expected-exe /usr/local/bin/pstv-demo-cart \
  --state /tmp/vita-control-b8.next status --machine
vita-control --pidfile /run/pstv-demo-cart.pid \
  --expected-exe /usr/local/bin/pstv-demo-cart \
  --state /tmp/vita-control-b8.next next --machine
```

Status matched the known-good cart:

```text
pid: 28289
running: 1
exe: /usr/local/bin/pstv-demo-cart
expected_exe: /usr/local/bin/pstv-demo-cart
target_executable_match: 1
next_signal: SIGUSR1
min_interval_ms: 500
```

One explicit `next` returned `status=triggered`, `pid=28289`, and
`signal=SIGUSR1`. The framebuffer hash changed after the scene advance:

```text
before: 929b6b44c988cc3d01c12aadb584b66b5f211893e753adc7c2f57e5aab0b977f
after:  b2843a77b8278f64182f2602ae1d7eb8ada716835926f1dca1f7fae6275aae22
```

The cart PID and executable remained unchanged. The live `fbcon` baseline was
`1` during this run and remained `1`; no fbcon change was made. CPUs remained
`0-3`, and the severe kernel fault scan remained clean.

## Cleanup

The read-only SquashFS loop mount and removable-storage mount were removed.
The temporary control rate-state file, copied `/tmp` image, and temporary mount
directory were absent after cleanup. The persistent B8 image remained on the
removable partition with the verified hash, and the known-good cart remained
running.
