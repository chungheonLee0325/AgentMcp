#!/usr/bin/env python3
"""Compares two play session captures of the same size (standard library only).

Usage:
  python capture_compare.py BEFORE.png AFTER.png [--region LEFT TOP RIGHT BOTTOM] [--threshold 24] [--name NAME] [--out FOLDER]

Prints the mean color difference inside the region, the share of its pixels that changed by more than the threshold and the box
around them. Writes <out>/<name>_diff.png, in which changed pixels are red, and <out>/<name>.html with the before, after and difference
images side by side and the region marked. Use it for the before and after of a change to layout, style or art, and for the kit gallery
against its baseline. Animated or changing parts of a screen (timers, progress) also differ; choose a region without them.

--out defaults to Saved/UiStyle of the current folder.
Exit code: 0, or 2 when the captures cannot be read or differ in size.
"""

import argparse
import base64
import datetime
import html
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from png_io import read_png, write_png  # noqa: E402

STYLE = """
body { margin: 0; padding: 20px; background: #10141b; color: #e8edf3; font: 14px/1.5 "Segoe UI", "Malgun Gothic", sans-serif; }
h1 { margin: 0 0 4px; font-size: 20px; } .muted { color: #8d9aab; }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(380px, 100%), 1fr)); gap: 14px; margin-top: 14px; }
figure { margin: 0; } figcaption { margin-bottom: 6px; color: #aab6c4; }
.frame { position: relative; } .frame img { display: block; width: 100%; border-radius: 6px; }
.region { position: absolute; border: 2px dashed #ff4d4d; box-sizing: border-box; pointer-events: none; }
"""


def data_uri(path):
    with open(path, "rb") as handle:
        return "data:image/png;base64," + base64.b64encode(handle.read()).decode("ascii")


def main():
    parser = argparse.ArgumentParser(description="Compares two captures of the same size.")
    parser.add_argument("before")
    parser.add_argument("after")
    parser.add_argument("--region", type=int, nargs=4, metavar=("LEFT", "TOP", "RIGHT", "BOTTOM"), help="pixels to compare; default: all")
    parser.add_argument("--threshold", type=int, default=24, help="largest channel difference that does not count as a change")
    parser.add_argument("--name", help="file name of the report without extension")
    parser.add_argument("--out", default=os.path.join("Saved", "UiStyle"), help="folder of the report")
    args = parser.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")

    try:
        width, height, before = read_png(args.before)
        after_width, after_height, after = read_png(args.after)
    except (OSError, ValueError) as error:
        print(f"Cannot read the captures: {error}", file=sys.stderr)
        return 2
    if (width, height) != (after_width, after_height):
        print(f"The captures differ in size: {width}x{height} and {after_width}x{after_height}. Capture both at the same viewport size.",
              file=sys.stderr)
        return 2
    left, top, right, bottom = args.region or (0, 0, width, height)
    left, top, right, bottom = max(0, left), max(0, top), min(width, right), min(height, bottom)
    if left >= right or top >= bottom:
        print("The region is empty.", file=sys.stderr)
        return 2

    difference = bytearray(width * height * 4)
    total = changed = summed = 0
    box = [right, bottom, left - 1, top - 1]
    for y in range(height):
        row_inside = top <= y < bottom
        for x in range(width):
            index = (y * width + x) * 4
            delta = max(abs(before[index] - after[index]), abs(before[index + 1] - after[index + 1]), abs(before[index + 2] - after[index + 2]))
            inside = row_inside and left <= x < right
            if inside:
                total += 1
                summed += delta
            if inside and delta > args.threshold:
                changed += 1
                box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
                difference[index], difference[index + 1], difference[index + 2] = 255, max(0, 90 - delta // 3), 60
            else:
                shade = (after[index] + after[index + 1] + after[index + 2]) // (7 if inside else 15)
                difference[index] = difference[index + 1] = difference[index + 2] = shade
            difference[index + 3] = 255

    name = args.name or "compare_" + datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    difference_path = os.path.join(out, name + "_diff.png")
    write_png(difference_path, width, height, difference)

    result = (f"region {left},{top}-{right},{bottom}: mean difference {summed / total:.2f}; {changed} pixels ({changed / total:.2%}) changed by "
              f"more than {args.threshold}" + (f", within {box[0]},{box[1]}-{box[2]},{box[3]}" if changed else ""))
    region = (f"left:{left / width * 100:.3f}%;top:{top / height * 100:.3f}%;width:{(right - left) / width * 100:.3f}%;"
              f"height:{(bottom - top) / height * 100:.3f}%")
    figures = "".join(
        f"<figure><figcaption>{html.escape(label)}</figcaption><div class='frame'><img src='{uri}' alt=''>"
        f"<div class='region' style='{region}'></div></div></figure>"
        for label, uri in (("Before: " + os.path.basename(args.before), data_uri(args.before)),
                           ("After: " + os.path.basename(args.after), data_uri(args.after)),
                           ("Changed pixels in red", data_uri(difference_path))))
    page = (f"<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
            f"<title>Capture comparison {html.escape(name)}</title><style>{STYLE}</style></head><body>"
            f"<h1>Capture comparison</h1><div class='muted'>{html.escape(result)}</div><div class='grid'>{figures}</div></body></html>\n")
    with open(os.path.join(out, name + ".html"), "w", encoding="utf-8") as handle:
        handle.write(page)

    print(result)
    print(f"Report: {os.path.join(out, name + '.html')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
