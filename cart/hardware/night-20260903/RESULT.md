# Demo-cart temporary candidate cycle — 2026-09-03

## Scope

This was a temporary, reversible validation cycle on the PSTV. The running
rollback image did not contain an installed cart, so the frozen known-good
binary and the repaired candidate were both staged under `/tmp`. Nothing was
installed persistently and the known-good production path was not replaced.

Target: PSTV `192.168.18.43`
Kernel: `6.12.0-gc477a244a49b`
Outer source tree: `25c884c5a261b0f907bc06e944f1e7c2a785bcf4`
Cart repair lineage: `41b318a` (`fix: keep PSTV presentation at realtime frame rate`)

## Artifact identity

- Frozen baseline binary: `/home/cassie/projects/vita-linux-research/lab/demo-cart/pstv-demo-cart.arm`
  - size: `573392` bytes
  - SHA-256: `4eee4b0676a546b1d576af8a7f3f97a7ad01d6e9db95a238b3a9ac785fadae0b`
- Repaired candidate: `cart/build/pstv-demo-cart.arm`
  - size: `580180` bytes
  - SHA-256: `f1fa752c48bdbd1572fbbbb3f09f724120741a68955b79cab1ed07139f9a6005`
- Both binaries were uploaded to the target and their hashes matched the host.
- Start/stop wrappers were also hash-checked before execution.

## Machine-recorded lifecycle

1. Baseline started from `/tmp`, its executable path was verified through
   `/proc/<pid>/exe`, and a complete 1280x720 framebuffer was captured.
2. Baseline stopped through the wrapper; framebuffer console returned to bound.
3. Candidate started from `/tmp`, and its executable path was verified through
   `/proc/<pid>/exe`.
4. Candidate ran for approximately 60 seconds.
5. Candidate log reached 1,800 frames. Reported frame heartbeats were:

   ```text
   frames=300 scene=1 fps=30.07 dropped=0
   frames=600 scene=2 fps=25.50 dropped=0
   frames=900 scene=4 fps=28.05 dropped=0
   frames=1200 scene=5 fps=30.00 dropped=0
   frames=1500 scene=0 fps=30.00 dropped=0
   frames=1800 scene=1 fps=30.00 dropped=0
   ```

6. Candidate stopped through the wrapper; `fbcon=1` was verified.
7. Final target state remained `online=0-3`, `fbcon=1`.
8. The temporary target directory was removed and verified absent.
9. Post-run `dmesg` contained 248 lines and produced `fault_hits=0` for the
   case-sensitive panic/Oops/BUG/data-abort/lockup scan.

## Frame evidence

- Baseline frame: 3,686,400 bytes, SHA-256
  `604138ab0cc67f897e08247a2d5c31581eb2bb5938e111371a1c4ccf4f6fb162`
- Candidate frame: 3,686,400 bytes, SHA-256
  `3f68854991667681cd53788f80eff869b3788285cbb624745f63ba2c469095f9`

The candidate frame was decoded as 1280x720 RGBA and visually inspected. It
showed a complete, coherent `CANDY VORTEX` scene with correct orientation,
centered composition, and no obvious clipping, console overwrite, blanking, or
severe corruption.

A single live framebuffer capture cannot establish temporal smoothness or
rule out an intermittent problem. The sustained runtime log and zero dropped
frames provide the stronger temporal evidence for this cycle.

## Result

**PASS — temporary candidate lifecycle.** The repaired candidate ran on the
real PSTV for the sustained window, produced valid framebuffer output, kept
four CPUs online, reported zero dropped frames, survived the post-run fault
scan, restored fbcon, and left no temporary target files.

This is not yet a persistent production-promotion record: the rollback image
has no embedded cart, and no fresh external video artifact was available for
the real-capture photosensitivity checker.
