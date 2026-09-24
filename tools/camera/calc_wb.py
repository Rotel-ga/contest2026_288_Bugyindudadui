#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Derive ISP white-balance gains from a captured frame.

There is no AWB in this pipeline, and raw Bayer has twice as many green
photosites as red or blue, so an uncorrected frame is strongly green.  This
fits grey-world gains to a real capture and prints them in the format the ISP
WBG stage expects.

Two gain sets are reported:

  normalised  - the reciprocals of the channel means scaled so the largest gain
                is exactly 1.0x.  Equalises the channels without clipping
                anything, and pulls overall brightness down.  This is what
                p4x_camera_csi.c ships.
  grey-world  - G held at 1.0x with R and B boosted.  Keeps the brightness but
                clips wherever a boosted channel runs past full scale, so the
                predicted clip fraction is printed alongside.

Gain format: 12-bit, 256 = 1.0x (Q4.8), values below 256 attenuate.  Taken from
the register default in
soc/esp32p4/register/hw_ver3/soc/isp_struct.h ("wbg_r ... default: 256").

Usage:
    tools/camera/calc_wb.py                       # out/camera/thumb.rgb565
    tools/camera/calc_wb.py path/to/thumb.rgb565
"""

import argparse
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_RAW = REPO / "out" / "camera" / "thumb.rgb565"

ONE = 256          # 1.0x in Q4.8
MAX_GAIN = 4095    # 12-bit field


def quantise(gain):
    return max(0, min(MAX_GAIN, int(round(gain * ONE))))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("raw", nargs="?", type=Path, default=DEFAULT_RAW,
                    help="RGB565 little-endian frame "
                         "(default: out/camera/thumb.rgb565)")
    args = ap.parse_args()

    if not args.raw.is_file():
        print(f"error: no such frame: {args.raw}\n"
              f"run tools/camera/decode_thumb.py first", file=sys.stderr)
        return 1

    data = args.raw.read_bytes()
    count = len(data) // 2
    if count == 0:
        print("error: empty frame", file=sys.stderr)
        return 1

    pixels = [data[i * 2] | (data[i * 2 + 1] << 8) for i in range(count)]

    # Normalise per channel before comparing: the field widths differ.
    red = [((p >> 11) & 0x1F) / 31.0 for p in pixels]
    green = [((p >> 5) & 0x3F) / 63.0 for p in pixels]
    blue = [(p & 0x1F) / 31.0 for p in pixels]

    mean_r = sum(red) / count
    mean_g = sum(green) / count
    mean_b = sum(blue) / count

    print(f"samples : {count} pixels")
    print(f"means   : R={mean_r:.4f}  G={mean_g:.4f}  B={mean_b:.4f}")
    print(f"ratios  : R/G={mean_r / mean_g:.3f}  B/G={mean_b / mean_g:.3f}")
    if min(mean_r, mean_g, mean_b) <= 0.0:
        print("error: a channel is entirely zero; capture a real frame first",
              file=sys.stderr)
        return 1

    # --- normalised: largest gain pinned to 1.0x, no clipping ---
    inv = (1.0 / mean_r, 1.0 / mean_g, 1.0 / mean_b)
    scale = max(inv)
    norm = tuple(v / scale for v in inv)
    qn = tuple(quantise(v) for v in norm)

    print("\nnormalised (recommended, no clipping):")
    print(f"  gains : R={norm[0]:.3f}x  G={norm[1]:.3f}x  B={norm[2]:.3f}x")
    print(f"  Q4.8  : gain_r={qn[0]}  gain_g={qn[1]}  gain_b={qn[2]}")
    print(f"  brightness scaled by about {sum(norm) / 3:.2f}x")

    # --- grey-world: G fixed at 1.0x ---
    gw = (mean_g / mean_r, 1.0, mean_g / mean_b)
    qg = tuple(quantise(v) for v in gw)
    clip_r = sum(1 for v in red if v * gw[0] > 1.0)
    clip_b = sum(1 for v in blue if v * gw[2] > 1.0)

    print("\ngrey-world (keeps brightness, may clip):")
    print(f"  gains : R={gw[0]:.3f}x  G=1.000x  B={gw[2]:.3f}x")
    print(f"  Q4.8  : gain_r={qg[0]}  gain_g={qg[1]}  gain_b={qg[2]}")
    print(f"  clip  : R {clip_r} ({clip_r * 100 / count:.1f}%), "
          f"B {clip_b} ({clip_b * 100 / count:.1f}%)")

    print("\npaste into app/p4x_selftest/p4x_camera_csi.c:")
    print(f"  #define SC2336_WB_GAIN_R  {qn[0]}")
    print(f"  #define SC2336_WB_GAIN_G  {qn[1]}")
    print(f"  #define SC2336_WB_GAIN_B  {qn[2]}")
    print("\nNote: this is a static fit to one scene. Re-run it after a "
          "lighting change; it is not a substitute for AWB.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
