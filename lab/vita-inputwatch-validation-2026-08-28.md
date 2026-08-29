# `vita-inputwatch` hardware smoke validation — 2026-08-28

B15 adds a static ARM input watcher. It selects an event node by `phys`, opens
that node with `O_RDONLY|O_NONBLOCK|O_CLOEXEC`, polls for a bounded duration,
and reports raw evdev records plus summary counts. It does not write input
state, sysfs, the cart, or the framebuffer.

## Vita 1000 — firmware 3.65

```text
selected_event=event1
selected_device=/dev/input/event1
selected_phys=vita_syscon_buttons
status=timeout
events_seen=0
key_presses=0
key_releases=0
key_repeats=0
abs_events=0
syn_reports=0
exit=1
```

The one-second bounded timeout is an expected result when no button is pressed;
exit status `1` distinguishes timeout from a complete event-stream drain.

## PSTV — firmware 3.60

```text
selected_event=event0
selected_device=/dev/input/event0
selected_phys=vita_syscon_buttons
status=timeout
events_seen=0
key_presses=0
key_releases=0
key_repeats=0
abs_events=0
syn_reports=0
exit=1
```

## Safety/result

The ARM binary was copied to `/tmp` only, executed for one bounded second, and
removed on each target. Both targets selected the correct button device despite
different event numbering. The next hardware pass should use a deliberately
pressed button and a front/rear touch observation, still with bounded duration
and no cart integration.
