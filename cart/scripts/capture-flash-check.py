#!/usr/bin/env python3
"""WCAG general-flash checker for recorded PSTV capture video.

Companion to `tests/test_photosensitivity.c`, which enforces the same
thresholds on host-rendered frames. This script applies them to a VIDEO
RECORDING of real hardware output, so the cart can be validated on-device
without a person watching it strobe.

    Intended use (hardware re-validation protocol):
      1. Record the PSTV output to video. Nobody watches live.
      2. Run this script over the recording.
      3. Only if it exits 0 does anyone view the footage.

    See cart/hardware/PHOTOSENSITIVITY.md.

Usage:
    capture-flash-check.py capture.mp4
    capture-flash-check.py capture.mp4 --grid 4 --json report.json
    capture-flash-check.py --ppm-dir frames/ --fps 30

Thresholds (WCAG 2.x general flash):
  - flash pair : adjacent frames whose relative-luminance delta >= 0.10
                 AND the darker frame's luminance < 0.80
  - rate limit : no more than 3 flash pairs in any one-second window

Why linear-light downscaling: WCAG relative luminance is the mean of a
NONLINEAR per-pixel function. Averaging sRGB bytes and then linearizing is
not the same number. ffmpeg's zscale converts to linear light BEFORE the
resize, so the downscaled mean equals the full-resolution mean.

Why a grid: a global frame average can hide a bright localized strobe. Each
grid cell is checked independently against the same thresholds, so a flash
confined to part of the screen is still caught.

Requires: ffmpeg + ffprobe (zscale filter). No Python dependencies.

Exit codes: 0 = clean, 1 = flash pairs present, 2 = rate limit exceeded,
3 = input/tooling error.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from array import array
from typing import NoReturn

FLASH_DELTA = 0.10
DARK_LIMIT = 0.80
FLASHES_PER_SECOND = 3

# WCAG relative luminance coefficients, applied to LINEAR light.
LUMA_R, LUMA_G, LUMA_B = 0.2126, 0.7152, 0.0722


def die(message: str) -> NoReturn:
    print(f"error: {message}", file=sys.stderr)
    sys.exit(3)


def require_tools() -> None:
    for tool in ("ffmpeg", "ffprobe"):
        if shutil.which(tool) is None:
            die(f"{tool} not found on PATH")


def probe_video(path: str) -> tuple[float, int, int, int]:
    """Return (fps, width, height, nb_frames_estimate)."""
    result = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=r_frame_rate,width,height,nb_frames",
         "-of", "json", path],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        die(f"ffprobe failed on {path}: {result.stderr.strip()}")
    try:
        stream = json.loads(result.stdout)["streams"][0]
    except (KeyError, IndexError, json.JSONDecodeError):
        die(f"no video stream found in {path}")
        raise  # unreachable; keeps `stream` provably bound below

    rate = stream.get("r_frame_rate", "0/0")
    numerator, _, denominator = rate.partition("/")
    try:
        fps = float(numerator) / float(denominator)
    except (ValueError, ZeroDivisionError):
        fps = 0.0
    if fps <= 0:
        die(f"could not determine frame rate of {path}")

    frames = stream.get("nb_frames")
    try:
        frame_count = int(frames)
    except (TypeError, ValueError):
        frame_count = 0
    return fps, int(stream["width"]), int(stream["height"]), frame_count


def stream_frames(path: str, cols: int, rows: int):
    """Yield frames as array('H') of linear-light RGB16, row-major.

    zscale=t=linear converts sRGB to linear light first; the resize then
    averages in linear light, which is what makes the downscaled mean equal
    the full-resolution mean. rgb48le keeps 16 bits so linear darks do not
    band.
    """
    # Capture files often carry no colorspace tags, and zscale then fails with
    # "no path between colorspaces". Assert bt709/sRGB primaries on the input
    # side (correct for HDMI capture of this cart) so the conversion is
    # well-defined, then convert to linear light before the resize.
    chain = (
        "zscale=min=bt709:pin=bt709:tin=bt709:m=bt709:p=bt709:t=linear,"
        f"scale={cols}:{rows}:flags=area,format=rgb48le"
    )
    command = [
        "ffmpeg", "-v", "error", "-nostdin", "-i", path,
        "-vf", chain,
        "-f", "rawvideo", "-pix_fmt", "rgb48le", "-",
    ]
    frame_bytes = cols * rows * 3 * 2
    process = subprocess.Popen(command, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    stdout = process.stdout
    stderr_pipe = process.stderr
    assert stdout is not None and stderr_pipe is not None
    try:
        while True:
            chunk = stdout.read(frame_bytes)
            if not chunk:
                break
            if len(chunk) < frame_bytes:
                break
            values = array("H")
            values.frombytes(chunk)
            if sys.byteorder != "little":
                values.byteswap()
            yield values
    finally:
        stdout.close()
        stderr = stderr_pipe.read().decode(errors="replace")
        process.wait()
        if process.returncode not in (0, None) and stderr.strip():
            print(f"warning: ffmpeg reported: {stderr.strip()}", file=sys.stderr)


def luminance_global(values: array) -> float:
    """Mean WCAG relative luminance of a linear-light RGB16 frame."""
    red = sum(values[0::3])
    green = sum(values[1::3])
    blue = sum(values[2::3])
    pixels = len(values) // 3
    return (LUMA_R * red + LUMA_G * green + LUMA_B * blue) / (pixels * 65535.0)


def luminance_cells(values: array, cols: int, rows: int, grid: int) -> list[float]:
    """Mean luminance per grid cell, reading order."""
    cell_w = max(1, cols // grid)
    cell_h = max(1, rows // grid)
    out = []
    for gy in range(grid):
        y0 = gy * cell_h
        y1 = rows if gy == grid - 1 else (gy + 1) * cell_h
        for gx in range(grid):
            x0 = gx * cell_w
            x1 = cols if gx == grid - 1 else (gx + 1) * cell_w
            total = 0.0
            count = 0
            for y in range(y0, y1):
                base = y * cols * 3
                for x in range(x0, x1):
                    index = base + x * 3
                    total += (LUMA_R * values[index]
                              + LUMA_G * values[index + 1]
                              + LUMA_B * values[index + 2])
                    count += 1
            out.append(total / (count * 65535.0) if count else 0.0)
    return out


def is_flash_pair(first: float, second: float) -> bool:
    return abs(second - first) >= FLASH_DELTA and min(first, second) < DARK_LIMIT


def analyse(series: list[float], fps: float, label: str) -> dict:
    """Flash-pair and rate analysis over one luminance series."""
    flags = []
    worst_delta = 0.0
    worst_index = -1
    for index in range(1, len(series)):
        delta = abs(series[index] - series[index - 1])
        if delta > worst_delta:
            worst_delta = delta
            worst_index = index
        flags.append(is_flash_pair(series[index - 1], series[index]))

    window = max(1, int(round(fps)))
    worst_rate = 0
    worst_rate_at = -1
    for start in range(0, max(1, len(flags) - window + 1)):
        count = sum(flags[start:start + window])
        if count > worst_rate:
            worst_rate = count
            worst_rate_at = start
    return {
        "label": label,
        "frames": len(series),
        "flash_pairs": sum(flags),
        "worst_delta": worst_delta,
        "worst_delta_frame": worst_index,
        "worst_rate_per_second": worst_rate,
        "worst_rate_at_frame": worst_rate_at,
        "rate_exceeded": worst_rate > FLASHES_PER_SECOND,
        "flash_frames": [i + 1 for i, f in enumerate(flags) if f][:50],
    }


def read_ppm(path: str) -> tuple[int, int, bytes]:
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:2] != b"P6":
        raise ValueError(f"{path}: not a P6 PPM")
    position = 2
    tokens = []
    # The "P6" magic is already consumed, so only three header tokens remain:
    # width, height, maxval. Reading four here would run into pixel data.
    while len(tokens) < 3:
        while position < len(data) and data[position] in b" \t\r\n":
            position += 1
        if position < len(data) and data[position] == 0x23:
            while position < len(data) and data[position] not in b"\r\n":
                position += 1
            continue
        start = position
        while position < len(data) and data[position] not in b" \t\r\n":
            position += 1
        tokens.append(data[start:position])
    width, height, maxval = int(tokens[0]), int(tokens[1]), int(tokens[2])
    if maxval != 255:
        raise ValueError(f"{path}: only 8-bit PPM supported")
    # Exactly one whitespace byte separates the header from the raster.
    body = data[position + 1:]
    if len(body) < width * height * 3:
        raise ValueError(f"{path}: truncated pixel data")
    return width, height, body[:width * height * 3]


def srgb_to_linear_table() -> list[float]:
    table = []
    for value in range(256):
        c = value / 255.0
        table.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return table


def ppm_luminance(pixels: bytes, table: list[float]) -> float:
    """Exact WCAG luminance via a 256-bin histogram per channel.

    bytes.count() runs in C, so this is 768 fast scans rather than a
    per-pixel Python loop.
    """
    total = 0.0
    for offset, weight in ((0, LUMA_R), (1, LUMA_G), (2, LUMA_B)):
        plane = pixels[offset::3]
        for value in set(plane):
            total += weight * table[value] * plane.count(value)
    return total / (len(pixels) // 3)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("video", nargs="?", help="capture video (mp4, mkv, ...)")
    parser.add_argument("--ppm-dir", help="analyse a directory of PPM frames instead")
    parser.add_argument("--ppm-glob", default="*.ppm")
    parser.add_argument("--fps", type=float,
                        help="override frame rate (required with --ppm-dir)")
    parser.add_argument("--cols", type=int, default=32,
                        help="downscale width for analysis (default 32)")
    parser.add_argument("--rows", type=int, default=18,
                        help="downscale height for analysis (default 18)")
    parser.add_argument("--grid", type=int, default=4,
                        help="NxN localized-flash grid; 0 disables (default 4)")
    parser.add_argument("--json", help="write a machine-readable report here")
    parser.add_argument("--quiet", action="store_true",
                        help="suppress the per-frame table")
    args = parser.parse_args()

    if not args.video and not args.ppm_dir:
        parser.error("give a video file or --ppm-dir")

    results = []
    if args.ppm_dir:
        import glob as globmod
        files = sorted(globmod.glob(os.path.join(args.ppm_dir, args.ppm_glob)))
        if len(files) < 2:
            die("need at least 2 PPM frames")
        fps = args.fps or 30.0
        table = srgb_to_linear_table()
        series = []
        for path in files:
            _w, _h, pixels = read_ppm(path)
            series.append(ppm_luminance(pixels, table))
        source = args.ppm_dir
        results.append(analyse(series, fps, "global"))
    else:
        require_tools()
        if not os.path.exists(args.video):
            die(f"{args.video}: no such file")
        fps, width, height, _count = probe_video(args.video)
        if args.fps:
            fps = args.fps
        source = args.video

        globals_series: list[float] = []
        cell_series: list[list[float]] = []
        for frame in stream_frames(args.video, args.cols, args.rows):
            globals_series.append(luminance_global(frame))
            if args.grid > 0:
                cell_series.append(
                    luminance_cells(frame, args.cols, args.rows, args.grid))
        if len(globals_series) < 2:
            die("fewer than 2 frames decoded")

        print(f"source     : {source}")
        print(f"resolution : {width}x{height}  ->  analysed at "
              f"{args.cols}x{args.rows} (linear-light area average)")
        print(f"frame rate : {fps:g} fps")
        print(f"frames     : {len(globals_series)} "
              f"({len(globals_series) / fps:.2f}s)")
        print()

        results.append(analyse(globals_series, fps, "global"))
        if args.grid > 0 and cell_series:
            cells = len(cell_series[0])
            for cell in range(cells):
                gy, gx = divmod(cell, args.grid)
                results.append(analyse([f[cell] for f in cell_series], fps,
                                       f"cell r{gy}c{gx}"))

    if not args.quiet and not args.ppm_dir:
        pass

    worst = max(results, key=lambda r: (r["rate_exceeded"], r["flash_pairs"],
                                        r["worst_delta"]))
    print(f"{'region':>12} {'frames':>7} {'flashes':>8} "
          f"{'worst d':>9} {'max/sec':>8}  verdict")
    for entry in results:
        verdict = "ok"
        if entry["rate_exceeded"]:
            verdict = "** RATE EXCEEDED **"
        elif entry["flash_pairs"]:
            verdict = "** FLASH PAIRS **"
        print(f"{entry['label']:>12} {entry['frames']:>7} "
              f"{entry['flash_pairs']:>8} {entry['worst_delta']:>9.4f} "
              f"{entry['worst_rate_per_second']:>8}  {verdict}")

    print()
    print(f"thresholds : delta >= {FLASH_DELTA}, darker < {DARK_LIMIT}, "
          f"max {FLASHES_PER_SECOND} flashes/sec")

    if worst["rate_exceeded"]:
        print(f"VERDICT: FAIL — {worst['label']} exceeds the flash rate limit "
              f"({worst['worst_rate_per_second']}/sec near frame "
              f"{worst['worst_rate_at_frame']})")
        status = 2
    elif worst["flash_pairs"]:
        print(f"VERDICT: FAIL — {worst['label']} has {worst['flash_pairs']} "
              f"flash pair(s), worst delta {worst['worst_delta']:.4f} "
              f"at frame {worst['worst_delta_frame']}")
        status = 1
    else:
        print(f"VERDICT: PASS — no flash pairs in any region "
              f"(worst delta {worst['worst_delta']:.4f}, "
              f"{worst['worst_delta'] / FLASH_DELTA * 100:.0f}% of threshold)")
        status = 0

    if args.json:
        with open(args.json, "w") as handle:
            json.dump({"source": source, "regions": results,
                       "status": status}, handle, indent=2)
        print(f"report written to {args.json}")
    return status


if __name__ == "__main__":
    sys.exit(main())
