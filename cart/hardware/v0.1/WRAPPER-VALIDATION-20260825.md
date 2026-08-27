# PSTV Demo Cart Hardened Wrapper Validation

**UTC:** 2026-08-25T03:29:34Z
**Target:** PSTV / Dolce at `192.168.18.43`
**Scope:** Real-hardware validation of the hardened cart framebuffer-ownership wrappers before their cart-foundation PR. The wrappers were copied transiently to `/tmp` and removed after the test. No kernel, loader, rootfs, VitaOS-storage, or persistent target files were changed.

## Artifacts under test

| Artifact | SHA-256 |
|---|---|
| `start-demo-cart.sh` | `e9697b784f3d1db3fb310d6c6a78ef08b3eb47ef69da2119f989cb92c1fc7c7d` |
| `stop-demo-cart.sh` | `00e1cf6d70ac8d264b6692c53d813235451ba806eb003299bae4ef5bbd66e0ef` |
| Live cart executable | `4eee4b0676a546b1d576af8a7f3f97a7ad01d6e9db95a238b3a9ac785fadae0b` |

## Preconditions

- Existing cart PID: `13355`
- CPU online set: `0-3`
- `vtcon1/bind`: `0` (cart had safely unbound fbcon)
- Existing saved framebuffer: exactly `3,686,400` bytes

## Procedure and result

The hardened wrappers were copied to `/tmp/pstv-cart-{start,stop}-hardened.sh`, made executable, and used against the normal installed cart binary and `/run` ownership state.

### Cycle 1

1. Hardened stop matched and terminated cart PID `13355`, restored the saved framebuffer, and rebound fbcon.
   - `after_stop_fbcon=1`
   - `after_stop_online=0-3`
2. Hardened start saved exactly `3,686,400` bytes, unbound fbcon, and launched cart PID `27578`.
   - `after_start_fbcon=0`
   - `after_start_online=0-3`

### Cycle 2

1. Hardened stop matched and terminated cart PID `27578`, restored the saved framebuffer, and rebound fbcon.
   - `after_stop_fbcon=1`
   - `after_stop_online=0-3`
2. Hardened start saved exactly `3,686,400` bytes, unbound fbcon, and launched cart PID `27972`.
   - `after_start_fbcon=0`
   - `after_start_online=0-3`

## Fault scan and final state

The post-cycle scan found no kernel Oops, BUG, unhandled kernel fault, panic, or soft/hard lockup signatures. The transient `/tmp` wrapper files were removed. The cart was deliberately left running, as it was before testing:

- final PID: `27972`
- CPU online set: `0-3`
- `vtcon1/bind`: `0`

## Result

**PASS — hardened wrappers correctly performed live framebuffer save/restore and fbcon ownership transitions across two stop/start cycles on real PSTV hardware.**

Exact command output is preserved in `COMMAND-OUTPUT.txt` beside this report.
