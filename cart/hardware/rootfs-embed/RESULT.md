# Rootfs embedding — PSTV hardware validation (2026-08-25)

Fixes the "cart vanishes on reboot" pitfall found during the B3 cold-boot
cycle: `/usr/local/bin/pstv-demo-cart` + wrappers were RAM-resident deploy
artifacts; the rootfs is an initramfs, so every reboot wiped them.

## Change

Commit `72c7cbd`: production-baseline cart binary (SHA `4eee4b06…`, the
`verify-baseline.sh` pin) + current start/stop wrappers embedded at 0755 in
`buildroot-vita/board/vita/overlay/usr/local/bin/`. No autostart — kernel
bring-up keeps clean console boots; the fix is exactly "files survive
reboot."

## Deployment artifact

- Base: production kernel `996f030022f0` + production rootfs `ed6081d8…`
- Rootfs repacked surgically (single fakeroot invocation; `dev/console`
  verified char 5,1 after repack), zstd-19 → 16,427,395 bytes
- zImage relink: `1a742554a19fa6ab93f7eb80c924547300af4d2bdae3ddead55118e10cf15c63`
  (20,965,144 bytes); new rootfs byte-confirmed inside vmlinux, old rootfs absent
- Kernel `.config` matches the production pin `6a9dacb0…` exactly
- Pushed via FTP with full readback hash verification
- Rollback: production zImage `d7a30ebe…` archived in
  `lab/smp-re/production/artifacts/`

## Hardware gate (true cold boot)

1. Linux → VitaOS reset (busybox init TERM) → 1338 up ~20 s
2. FTP push + readback verify → 30 s settle → `launch PLGINLDR0` → Linux ~60 s
3. Fresh boot: all three files present at `/usr/local/bin/`, binary SHA
   exactly `4eee4b06…`, fault scan clean, CPUs `0-3`
4. Cart started from the embedded copy: pid 378, fbcon `0`, full-frame render
   captured (3,686,400 bytes, SHA `1ab4ac94…`), vision-QA pass (complete
   CANDY VORTEX, correct channels, no clipping/console overwrite; sporadic
   capture tear only, per the established QA rule), fault scan clean

## Statement

The B-series candidate contract is unchanged: candidates still run from
`/tmp` via `CART_BIN` overrides; `/usr/local/bin` remains the known-good
baseline, now reboot-persistent.
