# B4 hardware validation — persistent render worker pool

Date: 2026-08-25
Device: PSTV `192.168.18.43`
Kernel: production Vita Linux `6.12.0-g996f030022f0`
Implementation: merged PR #14, merge `c6a433de58ae5fd15f6d1b42fb3fc5befcc6aa4f`
Candidate build: `cart/build/pstv-demo-cart.arm`

## Candidate identity

- Candidate SHA-256: `37f368cfaca403102b1d0cc62ddaadd02647b722f03e5ee97142985b599d614b`
- Known-good baseline SHA-256: `4eee4b0676a546b1d576af8a7f3f97a7ad01d6e9db95a238b3a9ac785fadae0b`
- Candidate executed from `/tmp/pstv-demo-cart-b4-workers`; baseline was never overwritten.

## Cycle

The guarded cycle stopped the embedded known-good cart, started the candidate,
held it for 60 seconds, captured a raw framebuffer, stopped the candidate, and
restored the embedded baseline. The candidate stayed alive and the restoration
path completed normally.

Observed output:

```text
normal_pid=378
normal_sha256=4eee4b0676a546b1d576af8a7f3f97a7ad01d6e9db95a238b3a9ac785fadae0b
b4_pid=16218
b4_exe=/tmp/pstv-demo-cart-b4-workers
b4_sha256=37f368cfaca403102b1d0cc62ddaadd02647b722f03e5ee97142985b599d614b
b4_fbcon=0
b4_online=0-3
frames=300 scene=1 fps=30.08 dropped=0
frames=600 scene=2 fps=24.94 dropped=0
frames=900 scene=4 fps=28.12 dropped=0
frames=1200 scene=5 fps=30.00 dropped=0
frames=1500 scene=0 fps=30.00 dropped=0
b4_pid_alive=yes
b4_frame_bytes=3686400
b4_frame_sha256=738230aff0c0d415bf7da05736ba053c924a98b53ce19723bdb0a697f13ffef5
final_pid=16271
final_exe=/usr/local/bin/pstv-demo-cart
final_fbcon=0
final_online=0-3
final_save_bytes=3686400
fault_scan=clean
```

The post-cycle verification found exactly one cart process (`16271`), the
baseline SHA still exact, all CPUs online, and framebuffer console unbound.

## Frame QA

`b4-frame.raw` is a complete 1280x720x4 capture (3,686,400 bytes), matching
its remote SHA after transfer. Converted PNG QA shows the complete **GIRL MODE
MAXIMUM** scene: pastel pink/blue checkerboard, hearts, white title block,
and purple lettering. No clipping, black frame, console overwrite, or
rendering corruption was observed. Unsynchronized live-framebuffer `dd`
tearing remains a capture artifact when it occurs; this capture was visually
clean.

## Result

**PASS.** Persistent worker-pool candidate survived the sustained run, advanced
scenes, produced a complete framebuffer, passed the fault scan, and restored
the known-good cart. The PSTV was left in Linux with the embedded baseline
cart running.
