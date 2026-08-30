# Handheld Vita Linux/toolkit validation — 2026-08-28

Device: Vita 1000, `192.168.18.36`
Firmware: 3.65

## Boot-chain result

The handheld was initially running the official `baremetal-loader_363.skprx`
loader (the correct 3.63+ loader family) with an older payload and older
kernel. The DvaMishkiLapa replacement was tested but caused Plugin Loader to
crash back to LiveArea, so it was immediately rolled back and verified.

Restored loader SHA-256:

```text
15918da3d539fb1e4d75567da1174cd9568150f2700eab4406e08678ecccfded
```

The production SMP payload was then installed at `ux0:/baremetal/payload.bin`
and read back over VitaOS FTP:

```text
529d395acae3c212565cb88f3f291bb0d381837276db1841a76339b78727515a
105960 bytes
```

## Linux baseline after payload correction

```text
kernel=6.12.0-g202203335bde-dirty
arch=armv7l
cpu_online=0-3
processors=4
wifi=mlan0 192.168.18.36/24
framebuffer=960,544 32bpp
memory=493256 kB total
fault_scan=clean
```

The production payload corrected the handheld from `cpu_online=0,2` to all
four CPUs without changing the loader, kernel, DTB, or rootfs.

## Toolkit parity test

The handheld kernel does not expose SquashFS in `/proc/filesystems`, so the
B13 SquashFS image could not be mounted even though loop devices are present.
The image was extracted on the host and staged at `/opt/vita-toolkit` in the
handheld's RAM, matching TCC's compiled-in path. The B13 manifest-driven
native test then passed on ARM hardware:

```text
schema=1
action=info
toolchain_root=/opt/vita-toolkit
compiler=cc
compiler_path=/opt/vita-toolkit/bin/cc
schema=1
action=test
status=pass
source_count=2
expected_status=7
run_status=7
runtime_gate=pass
```

The temporary extracted toolkit and test files were removed after validation.

## Final parity result

The handheld kernel was rebuilt from the canonical `996f030` source line with
only the following filesystem options added to `vita_defconfig`:

```text
CONFIG_SQUASHFS=y
CONFIG_SQUASHFS_ZSTD=y
```

The new Vita 1000 artifacts were deployed and read back exactly:

```text
zImage:       e697977daec0c93adde67ab747de69689dbdfcf521f26eae2ab2307f4e8ba646  20983384 bytes
vita1000.dtb: fa8e6e58a36ebbbd002563a196a323f08f9ac5fcf4f99dbc14a257bbbd0c5798  5603 bytes
```

After reboot, `/proc/filesystems` contained `squashfs`. The B13 image mounted
with the intended read-only policy:

```text
/dev/loop0 /opt/vita-toolkit squashfs ro,nosuid,nodev,relatime,errors=continue 0 0
```

The same two-source manifest test passed from the mounted SquashFS payload:

```text
schema=1
action=info
toolchain_root=/opt/vita-toolkit
compiler=cc
compiler_path=/opt/vita-toolkit/bin/cc
schema=1
action=test
status=pass
source_count=2
expected_status=7
run_status=7
after_cpus=0-3
runtime_gate=pass
```

All temporary files and mounts were removed after the test. The official
3.65 `baremetal-loader_363.skprx` remains installed; the production SMP
payload remains at `ux0:/baremetal/payload.bin`.

## Result

**PASS.** The Vita 1000 and PSTV now share a validated local ARM development
workflow while retaining separate firmware/device boot adapters. The handheld
runs the production four-core payload, mounts the toolkit read-only, and
compiles/links/runs a multi-file native project on hardware.
