# HyFetch on Vita Linux

Vita Linux Next packages [HyFetch 1.99.0](https://github.com/hykilpikonna/hyfetch)
into the Buildroot image. The target uses HyFetch's `neofetch` backend because it
is already available as a small Buildroot package; the image does not need a
second native fetch implementation or a Python package manager.

## Image configuration

`buildroot-vita/configs/vita_defconfig` enables:

- `python3`
- Python readline support (HyFetch imports `readline` on Linux)
- Buildroot `neofetch`
- the external `python-hyfetch` package

`/root/.config/hyfetch.json` is installed with a rainbow preset, 8-bit colour
mode, and noninteractive `--stdout=off` backend arguments. The command therefore
works immediately over SSH or a serial shell:

```sh
hyfetch
hyfetch --backend neofetch --preset rainbow
hyfetch --help
```

The default config is target-specific and is deliberately not copied into the
external toolkit SquashFS. HyFetch is part of the embedded rescue image so the
system identity command remains available before removable storage is mounted.

## Build and verify

From the outer project root, apply the Buildroot configuration and build the
image. On this Debian host, pass the ARM toolchain explicitly when building the
kernel:

```sh
make rootfs-config
make rootfs LINUX_VITA_DIR=<kernel-worktree>
```

Before deploying, verify the package and config are present in the generated
target tree and in the embedded archive:

```sh
grep -E 'BR2_PACKAGE_(PYTHON_HYFETCH|PYTHON3|N...(truncated)