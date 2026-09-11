# B16 `vita-hwprobe` Implementation Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task. Preserve the host gates and do not deploy until the staged payload passes the external image and hardware gates.

**Goal:** Add one bounded, read-only machine inventory command that answers what Vita Linux system is running without requiring raw MMIO or new kernel interfaces.

**Architecture:** `vita-hwprobe` is a POSIX-shell dispatcher over the existing toolkit commands and stable sysfs/proc surfaces. It emits a versioned envelope with one clearly named section per existing tool, preserving each tool's `schema=1` records rather than inventing a second interpretation of hardware state. Missing optional surfaces are reported as `UNKNOWN` or a section error; the command never writes devices, framebuffer state, network state, or partitions.

**Tech Stack:** BusyBox/POSIX `sh`, existing `vita-diag`, `vita-netdiag`, `vita-storage`, `vita-usbinfo`, `vita-inputinfo`, and `vita-fb` machine interfaces; host fixture roots; static toolkit payload packaging.

---

## Acceptance contract

- `vita-hwprobe --machine` exits 0 for a healthy target and emits `schema=1`, `tool=vita-hwprobe`, target identity, and sections for diagnostics, network, storage, USB, input, and framebuffer metadata.
- `--section NAME` limits output to one known section; unknown sections fail closed with exit 2.
- No section performs a write or implicitly mounts storage, changes fbcon ownership, sends a signal, or probes raw MMIO.
- Output is bounded and parseable as `key=value` records; section boundaries are explicit.
- Host tests run from the repository root and from a different working directory using fixture roots/command seams.
- The staged manifest contains the command, documentation, and tests; toolkit version is bumped only after the feature is green.

## Tasks

### Task 1: Write the failing contract test

**Files:**
- Create: `toolkit/tests/test-vita-hwprobe.sh`
- Modify: `toolkit/README.md` planned sequence entry

Create fake proc/sysfs roots and fake section commands. Assert the complete machine envelope, section ordering, unknown-section exit 2, and no-write behavior. Run it against the current tree and record the expected RED failure.

### Task 2: Add the dispatcher skeleton

**Files:**
- Create: `toolkit/bin/vita-hwprobe`

Implement strict option parsing, repository-independent path resolution, section selection, bounded command execution, and explicit error records. Do not use shell evaluation or source configuration files. Make the default path machine-readable and keep human output out of the initial contract.

Run `sh -n toolkit/bin/vita-hwprobe` and the focused test; expect GREEN.

### Task 3: Add existing-tool section adapters

**Files:**
- Modify: `toolkit/bin/vita-hwprobe`
- Modify only if needed for fixture seams: the existing toolkit command wrappers

Add adapters for `vita-diag`, `vita-netdiag`, `vita-storage`, `vita-usbinfo`, `vita-inputinfo`, and framebuffer metadata. Preserve their output as namespaced records or explicit section blocks; do not flatten values in ways that lose device identity or `UNKNOWN` state.

Run the focused test, then each existing command's contract tests.

### Task 4: Add negative and safety coverage

**Files:**
- Modify: `toolkit/tests/test-vita-hwprobe.sh`

Cover missing optional directories, malformed command output, command timeout/failure, unknown section names, paths containing spaces, and execution from `/tmp`. Assert that no test fixture receives writes and that a failed section cannot silently become a successful empty report.

Run the focused test under ShellCheck and with a clean temporary environment.

### Task 5: Wire packaging and documentation

**Files:**
- Modify: `toolkit/squashfs/VERSION`
- Modify: `toolkit/README.md`
- Modify: `tools/build-vita-toolkit-squashfs.sh` only if the manifest is not automatically discovered

Bump the version after source/tests are green, document the output contract and examples, and verify the staged manifest includes `bin/vita-hwprobe` plus all required source/test files.

### Task 6: Run the full host gates

Run:

```sh
sh toolkit/tests/test-vita-hwprobe.sh
for t in toolkit/tests/test-*.sh; do sh "$t"; done
make -C cart complete-test CROSS_CC=arm-linux-gnueabihf-gcc
make test CART_CROSS_CC=arm-linux-gnueabihf-gcc
shellcheck toolkit/bin/vita-hwprobe toolkit/tests/test-vita-hwprobe.sh
```

Also run the toolkit manifest-only and stage-only packaging checks. Confirm the ARM payload identity with `file` and record the final manifest/hash.

### Task 7: Review and hand off

Stage only the B16 source, test, docs, and version files. Run `git diff --cached --check` and the staged safety scans. Obtain the two read-only reviews required by the toolkit workflow before committing. Hardware validation is a separate follow-up using the existing `/tmp` smoke route unless a deliberately rebuilt SquashFS-enabled image is selected.

## Explicit non-goals

- No raw syscon command interface.
- No MMIO probing or register writes.
- No framebuffer capture/restore or fbcon changes.
- No storage writes or implicit mounts.
- No authenticated network mutation surface.
- No kernel, DTB, rootfs, loader, Bluetooth, GPU, or OHCI changes.

## Dependency order

```text
Task 1 (RED contract)
    -> Task 2 (dispatcher)
    -> Task 3 (section adapters)
    -> Task 4 (negative/safety coverage)
    -> Task 5 (packaging/docs)
    -> Task 6 (full host gates)
    -> Task 7 (review and handoff)
```
