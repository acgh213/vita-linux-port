# Vita Linux Toolkit

The toolkit is a BusyBox-first, scriptable userspace layer for Vita Linux.
It is deliberately separate from the graphics cart: the cart demonstrates the
framebuffer; the toolkit explains and exercises the machine around it.

## Shape

- `toolkit/bin/vita-*` — on-device commands installed in
  `/usr/local/bin/` by the rootfs overlay.
- `toolkit/tests/` — host-side shell tests using a fake proc/sys/dev root.
- `tools/pstv-*` — host-side deployment, capture, and evidence helpers (next
  layer; not mixed into device commands).
- Output has two modes: human-readable by default, stable `key=value` lines
  with `--machine` for scripts and future network/API consumers.

## First command: `vita-diag`

Read-only inventory of the running Linux instance:

- kernel and architecture
- uptime and memory summary
- online CPU topology
- framebuffer geometry and pixel format
- visible network interfaces and operational state
- stable `/dev/vita/*` eMMC partition links

It does not require Python, Lua, curl, or a writable persistent filesystem.
`VITA_DIAG_ROOT` is a host-test seam; production use leaves it unset.

## Planned sequence

1. `vita-diag` — machine identity and baseline evidence.
2. `vita-net` / `vita-netdiag` — interfaces, DHCP state, route, DNS, gateway
   RTT, and a bounded throughput probe using tools actually present.
3. `vita-fb` — framebuffer metadata, safe capture/restore, and scene/cart
   ownership checks.
4. `vita-bench` — NEON/cache/fill-rate measurements, including worker-pool
   scaling once the benchmark contract is settled.
5. `vita-control` — a deliberately small local/network control surface for
   demos, with explicit read-only and mutating modes.

The toolkit's safety rule is simple: diagnostics are read-only by default;
network tests are bounded; framebuffer writes require an explicit subcommand;
raw partition access never happens implicitly.
