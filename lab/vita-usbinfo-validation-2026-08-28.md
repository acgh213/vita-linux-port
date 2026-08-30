# B9.1 `vita-usbinfo` validation — 2026-08-28

## Artifact

The corrected image was created externally because Hermes blocks the
`mksquashfs` image-construction command, then verified locally:

```text
path: dist/vita-toolkit-native-b9.1.squashfs
version: 0.5.1
bytes: 10604544
sha256: 93fbd7a49c4fcbdc639238f9ee4a32791afbbae8d3107debcb351b7ab380ceb9
manifest entries: 1531
manifest: byte-for-byte match with a fresh staged manifest
```

## Persistent deployment

The image was copied to the removable exFAT payload store as:

```text
/mnt/vita-storage/vita-toolkit-native-b9.1.squashfs
```

Target-side verification matched the host hash and size:

```text
target sha256: 93fbd7a49c4fcbdc639238f9ee4a32791afbbae8d3107debcb351b7ab380ceb9
target bytes: 10604544
```

## Runtime test

The payload was mounted at `/opt/vita-toolkit-b9.1` with:

```text
mount -t squashfs -o loop,ro,nosuid,nodev
```

The `/proc/mounts` entry confirmed:

```text
/dev/loop0 /opt/vita-toolkit-b9.1 squashfs ro,nosuid,nodev,relatime,errors=continue
```

The mounted payload's `vita-usbinfo --machine` reported four USB device nodes:

```text
1-1  Generic USB3.0 Card Reader  05e3:0749  speed=480  parent=usb1
2-1  Realtek USB 10/100 LAN     0bda:8152  speed=480  parent=usb2
usb1 Linux EHCI Host Controller 1d6b:0002 class=09
usb2 Linux EHCI Host Controller 1d6b:0002 class=09
```

The corrected block and mount mapping was:

```text
usb_1_block=sda,sdb,sdb1
usb_1_mounts=/mnt/vita-storage
usb_3_block=NONE
usb_3_mounts=NONE
usb_4_block=NONE
usb_4_mounts=NONE
```

The command emitted `usb_read_only=1`. It did not write sysfs, touch VBUS,
reset a controller, load a module, mount storage itself, or access partition
contents.

The first B9 image was replaced by this B9.1 build before finalization because
its root-hub records included descendant block devices. B9.1 excludes that
misleading attribution and includes the real `sdb1` partition mapping.

## State preservation and cleanup

Before and after the inventory run:

```text
cart pid: 28289
cart exe: /usr/local/bin/pstv-demo-cart
CPUs: 0-3
fbcon: 1
fault scan: clean
```

The cart identity, CPU range, and live `fbcon` baseline were unchanged. The
read-only loop mount and exFAT mount were removed. The temporary mount
 directory and copied `/tmp` image were absent after cleanup. The persistent
B9.1 image remained on the removable partition with the verified hash.
