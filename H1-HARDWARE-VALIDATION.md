# H1 — Hardware Validation Runbook (Syscon Transport Hardening)

**Gate:** H1 from the plan `2026-08-12_200735-vita-syscon-safe-telemetry.md`
**What is being proven:** The K1 branch (`topic/syscon-transport-hardening`) boots and behaves identically to the R0 baseline on physical hardware — bounded retries, no silent unknown-result success, no regressions in probe / input / RTC / Wi-Fi, clean return to VitaOS.
**Status:** Software gates green (KUnit 12/12, ARM build, outer tests, reviews approved). **No K1 image has ever been deployed to hardware.** This is the first physical run.

## Target facts (fill `target-vita.yaml` in the research lab first)

- Model: **PCH-1103** (Vita 1000), fw **3.65 + ensō**, loader plugin installed (`ux0:data/tai/kplugin.skprx`, payload at `ux0:baremetal/payload.bin`)
- UART: 115200 8N1 via USB-serial adapter (Tigard/FTDI), console `ttyS0`
- FTP: port 1337 (VitaShell); loader command port: 1338
- Storage: Sony memory card on ux0 — **no SD2Vita, no YAMT** (card in game slot does not init)
- WiFi: Marvell SD8787 via SDIF2, auto power-on at boot (pwrseq). Historically DHCP `192.168.1.175`
- Battery: unknown age; **no battery commands are sent during H1**

## Current artifact state (verified 2026-08-16)

- Kernel worktree: `/home/cassie/projects/vita-linux-k1` on `topic/syscon-transport-hardening`
  - `dbefb4858101` — `mfd: vita-syscon: add frame policy tests` (K1.1)
  - `0000b085bf4b` — `mfd: vita-syscon: bound busy response retries` (K1.2)
  - Base: `65c5a9613c69` (= pinned `vita-pstv` gitlink)
- **Blocker found:** the existing `zImage` (4,248,072 B) was built with `CONFIG_INITRAMFS_SOURCE=""` — it contains **no rootfs** and would panic at boot. It is NOT deployable.
- DTB `vita1000.dtb` (4,851 B) is current and valid; `vita2000.dtb` / `pstv.dtb` also present.

## Prep (server side)

```sh
cd /home/cassie/projects/vita-linux-r0-clean

# 1. Build the rootfs (first build on this machine; 30–90 min, needs network).
#    Installs rootfs.cpio.zst into the K1 kernel worktree.
make rootfs LINUX_VITA_DIR=/home/cassie/projects/vita-linux-k1

# 2. Re-apply vita_defconfig (restores CONFIG_INITRAMFS_SOURCE="rootfs.cpio.zst"),
#    then build the deployable image.
make config LINUX_VITA_DIR=/home/cassie/projects/vita-linux-k1 CROSS_COMPILE=arm-linux-gnueabihf-
make build  LINUX_VITA_DIR=/home/cassie/projects/vita-linux-k1 CROSS_COMPILE=arm-linux-gnueabihf-
make verify-dtb LINUX_VITA_DIR=/home/cassie/projects/vita-linux-k1 CROSS_COMPILE=arm-linux-gnueabihf-

# 3. Sanity + record hashes in the lab record.
ls -la /home/cassie/projects/vita-linux-k1/arch/arm/boot/zImage   # expect ~20–22 MB, not 4.2 MB
sha256sum /home/cassie/projects/vita-linux-k1/arch/arm/boot/zImage \
          /home/cassie/projects/vita-linux-k1/arch/arm/boot/dts/vita1000.dtb \
          /home/cassie/projects/vita-linux-k1/arch/arm/boot/dts/vita2000.dtb \
          /home/cassie/projects/vita-linux-k1/arch/arm/boot/dts/pstv.dtb
```

## On-Vita prep

1. Vita fully charged, boot to VitaOS, on the same LAN as this server.
2. UART adapter connected. **Do not connect while Linux is running** (cold-boot order).
3. Start the serial console with a named log:
   ```sh
   mkdir -p logs
   ./serial_log.py -o logs/h1-20260816.log      # Ctrl+] to quit
   ```
   (Log also available via `logs/latest.log` for `vita_cmd.sh` / `boot_watch.sh`.)
4. Confirm loader files are in place (one-time setup was done previously):
   ```sh
   curl -s ftp://<VITA_IP>:1337/ux0:/data/tai/
   curl -s ftp://<VITA_IP>:1337/ux0:/baremetal/
   ```
   Expect `kplugin.skprx` and `payload.bin`. If missing: `make push-setup VITA_IP=<VITA_IP>` then install `plugin_loader.vpk` via VitaShell.

## Deploy

```sh
make push VITA_IP=<VITA_IP>      # zImage + all 3 DTBs → ux0:/linux/
make boot VITA_IP=<VITA_IP>      # destroys any running Linux, launches loader, watches boot stages
```

`make boot` requires `serial_log.py` running (reads `logs/latest.log`). Stage timeouts assume a ~22 MB zImage. If boot_watch times out, `make watch` to keep watching; the console keeps logging either way.

## H1 over SSH (no UART — hotspot fallback)

Used when the UART tap is not yet soldered. Evidence is dmesg-based: the kernel ring buffer still contains the full boot-time probe sequence, so all post-boot checks remain valid. Lost: the serial timeline and visibility into pre-network failures (a kernel that dies before WiFi = dark screen, no text; recover with a long power-hold back to VitaOS).

Prep (server side, already done 2026-08-16):
- `local/root/.ssh/authorized_keys` = server's `id_ed25519.pub` (gitignored overlay)
- Pre-generated SSH host keys in `local/etc/ssh/` (avoids slow first-boot keygen)
- Rootfs rebuilt with the overlay; K1 zImage re-embedded; R0 baseline image built from `65c5a961`
- The rootfs wpa config associates with **open** hotspots by default (`key_mgmt=NONE`); for a WPA2 hotspot, add `local/etc/wpa_supplicant.conf` and rebuild rootfs

Procedure:
1. Vita in VitaOS, on the same LAN as the hotspot; FTP reachable on 1337.
2. **R0 first** (known-good pipeline proof): `make push VITA_IP=<ip>` with the R0 image in place (or `curl -T linux_vita/arch/arm/boot/zImage ftp://<ip>:1337/ux0:/linux/zImage`), then launch:
   ```sh
   echo "destroy" | nc -w 3 <ip> 1338
   echo "launch PLGINLDR0" | nc -w 3 <ip> 1338
   ```
3. Watch the Vita screen for loader text (loader draws to the framebuffer), then wait ~30 s for kernel + WiFi + sshd.
4. Find the Vita's address (hotspot client list, or `arp-scan --localnet`), then:
   ```sh
   ssh -o StrictHostKeyChecking=accept-new root@<vita-ip>
   ```
5. Confirm R0 boots clean: `uname -a`, `dmesg | grep -Ei 'vita-syscon|syscon|spi|rtc-vita'` — expect no errors.
6. **Push K1** and repeat: `curl -T /home/cassie/projects/vita-linux-k1/arch/arm/boot/zImage ftp://<ip>:1337/ux0:/linux/zImage`, relaunch (step 2), re-ssh.
7. Run the H1 checks below over SSH; capture `dmesg` and per-check outputs into `logs/h1-ssh-20260816/`.

Verification note: `CONFIG_DEBUG_FS=y` + `CONFIG_DYNAMIC_DEBUG=y` are enabled, so the optional dyndbg instrumentation works over SSH too.

## H1 checks (run in order; record everything)

Safety invariant the whole time: **eMMC/VitaOS partitions stay read-only** (`ro,noauto` in fstab). No writes, no battery commands, no raw syscon access.

1. **Kernel identity** — confirm this is the K1 build:
   ```sh
   uname -a
   cat /proc/version
   ```
2. **Probe clean** — no transport errors in the whole boot log:
   ```sh
   dmesg | grep -Ei 'vita-syscon|syscon|spi[0-9]|rtc-vita|baryon|hw_info'
   ```
   FAIL on: `-EREMOTEIO`, `-EBUSY`, `-EBADMSG`, `-EMSGSIZE`, SPI timeout, IRQ timeout, malformed frame, checksum mismatch.
3. **Buttons ≥ 2 min** — list devices, then hammer every physical button (PS, start, select, D-pad, face, L1/R1, L2/R2 via rear touch, sticks, volume):
   ```sh
   cat /proc/bus/input/devices
   timeout 130 evtest /dev/input/eventX   # use the gamepad device from the listing
   ```
   (If `evtest` is absent in rootfs: `timeout 130 dd if=/dev/input/eventX of=/tmp/buttons.bin` and verify nonzero byte count.)
4. **Touch ≥ 2 min** — front panel AND rear touchpad (rear is a separate input device):
   ```sh
   timeout 130 evtest /dev/input/eventY   # rear touchpad
   ```
   Verify both generate events. No input latency/regression.
5. **RTC — 100 reads**:
   ```sh
   for i in $(seq 1 100); do hwclock -r; sleep 0.2; done
   ```
   Time must stay monotonic/plausible (no jumps, no epoch resets, no errors).
6. **Wi-Fi baseline** (power path exercised automatically at boot):
   ```sh
   ip link                       # mlan0 present + UP
   ip addr                       # DHCP address (192.168.1.x)
   ping -c 3 <gateway>
   cat /sys/devices/platform/soc/e0a00000.spi/spi_master/spi0/spi0.0/wlan_power   # expect 1
   ```
7. **Busy-attempt observation** — K1 retries on 0x80/0x81 silently below the ceiling (by design, to avoid log spam) and emits exactly one rate-limited `dev_warn_ratelimited` at exhaustion (16 attempts). Therefore:
   - PASS signal: no `vita-syscon` warning containing command ID + attempt count, no `-EBUSY` anywhere in dmesg.
   - Any exhaustion warning is a **STOP condition** (stricter than the plan's 8-attempt concern, since sub-ceiling attempts are unobservable without instrumentation).
   - Optional extra visibility: `echo 'file drivers/mfd/vita-syscon.c +p' > /sys/kernel/debug/dynamic_debug/control` to surface `dev_dbg_ratelimited` lines (unknown-result path).
8. **Read-only storage check** (proves partitions mount normally, read-only):
   ```sh
   mount /mnt/ur0 && ls /mnt/ur0 && umount /mnt/ur0
   ```
   Never mount rw. (os0/vs0/ur0 are the fstab entries; ur0 is the one that historically mounts.)
9. **Reboot to VitaOS** — the return gate:
   ```sh
   reboot
   ```
   Serial should show the cold-reset path (syscon notifier powers off WiFi/BT/MSIF/game card first). VitaOS must boot to LiveArea with the memory card recognized. Confirm storage mounts normally in VitaOS (open VitaShell and see ux0, or any app using ux0).

## Evidence to record afterward

In `/home/cassie/projects/vita-linux-research/lab/`:

- `target-vita.yaml` — copy from `target-vita.example.yaml`, fill non-secret facts only
- `baseline-2026-08-16.md` — boot log summary + validation results:
  - artifact hashes and sizes (zImage + 3 DTBs), kernel commits (`0000b085`, `dbefb485`, base `65c5a961`)
  - serial log path (`logs/h1-20260816.log`)
  - per-check results: probe / buttons / touch / RTC / Wi-Fi / busy / storage / reboot
  - max observed busy-attempt signal (none / exhaustion warn), any error strings seen
  - stop-condition table with pass/fail per row

## Stop conditions (from the plan's validation matrix)

| Signal | Action |
|---|---|
| checksum / length / result errors | **Pause H1.** K1 pauses for source-and-hardware reconciliation. Do not merge. |
| `-EBUSY` exhaustion warning (≥8 busy attempts in ordinary traffic) | **Pause H1.** Capture log, revise bound from evidence. |
| Previously working command now `-EREMOTEIO` | **Pause H1.** Reconcile before continuing. |
| Input / RTC / Wi-Fi regression vs R0 baseline | **Pause H1.** |
| Timeout / IRQ timeout increase | **Pause H1.** |
| Anything writes to eMMC/VitaOS partitions or sends a battery command | **Abort immediately.** |

Pass = all H1 checks green, reboot to VitaOS clean. Then: merge K1 in the kernel fork, cut `pin/syscon-transport-hardening` (O2) from outer `main`, run outer CI.

## Files that matter

- Kernel worktree: `/home/cassie/projects/vita-linux-k1`
- Outer home base: `/home/cassie/projects/vita-linux-r0-clean` (Makefile, serial tooling)
- Plan: `vita-linux-p0-dtb-build/.hermes/plans/2026-08-12_200735-vita-syscon-safe-telemetry.md` (H1 = §5, matrix = §9)
- Lab records: `/home/cassie/projects/vita-linux-research/lab/`
