# PSTV demo cart — rootfs embedding

Makes the demo cart survive reboots. Before this, the cart binary and its
start/stop wrappers were scp'd into the RAM-resident initramfs at deploy time
and vanished on every reboot — `/usr/local/bin/` did not exist on a fresh
boot, and the documented B-series preflight ("normal pid alive") failed until
someone re-deployed by hand (hit during the B3 cold-boot cycle, 2026-08-25).

## What is embedded

| File | Role |
|---|---|
| `pstv-demo-cart` | Production-baseline renderer, SHA-256 `4eee4b0676a546b1d576af8a7f3f97a7ad01d6e9db95a238b3a9ac785fadae0b` — the same binary `verify-baseline.sh` pins as `PRODUCTION_DEMO_BINARY_SHA256` |
| `start-demo-cart.sh` | Current wrapper (CART_BIN-aware, fbcon bind/unbind, framebuffer save) |
| `stop-demo-cart.sh` | Current wrapper (TERM + restore) |

All three are mode 0755 in git.

## Deliberate non-features

- **No autostart init script.** The cart is opt-in: kernel bring-up cycles
  keep their clean console boots, and the B-series contract (known-good at
  `/usr/local/bin`, candidates from `/tmp` with `CART_BIN` overrides) is
  unchanged. The fix is exactly "the files survive reboot," nothing more.
- **The binary is the production baseline, not the newest B-series build.**
  The embedded cart must equal what `verify-baseline.sh` pins, so
  "known-good" stays a single well-defined artifact. Promoting a newer cart
  to baseline is a separate decision that updates the verifier pins and this
  overlay together.

## Updating

If the production baseline is ever promoted, replace the three files here in
the same commit that updates `cart/scripts/verify-baseline.sh` pins, so the
overlay and the verifier never disagree.
