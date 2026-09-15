#!/usr/bin/env python3
"""Extracts the palette of a style frame image (standard library only).

Usage:
  python style_palette.py IMAGE.png [--colors 10] [--compare Saved/UiStyle/<name>.json] [--name NAME] [--out FOLDER]

Writes <out>/<name>.json and <out>/<name>.html: the main colors of the image by share, marked as surface, light or accent, and the
small saturated accents that a palette by area misses, each as sRGB hex and as the linear values that Unreal colors store. With
--compare, the report of style_extract.py, each color gets the closest color that the Widget Blueprints use and its theme token.
Line widths, radii and spacing cannot be read from pixels this way; judge them next to the style frame in the kit gallery.

--out defaults to Saved/UiStyle of the current folder.
Exit code: 0, or 2 when the image or the comparison cannot be read.
"""

import argparse
import base64
import colorsys
import datetime
import html
import json
import math
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from png_io import read_png  # noqa: E402

# Colors whose sRGB channels differ by at most this much are merged into one palette entry.
SAME_COLOR = 14
SAMPLES = 60000


def to_linear(value):
    channel = value / 255
    return channel / 12.92 if channel <= 0.04045 else ((channel + 0.055) / 1.055) ** 2.4


def hex_color(rgb):
    return "#" + "".join(f"{value:02X}" for value in rgb)


def hex_rgb(text):
    return tuple(int(text[index:index + 2], 16) for index in (1, 3, 5))


def hsv(rgb):
    return colorsys.rgb_to_hsv(*(value / 255 for value in rgb))


def widest(pixels):
    ranges = [max(pixel[axis] for pixel in pixels) - min(pixel[axis] for pixel in pixels) for axis in range(3)]
    axis = max(range(3), key=lambda index: ranges[index])
    return axis, ranges[axis]


def median_cut(pixels, count):
    """(average color, pixel count) of up to count boxes; the box with the widest range, weighted by its size, is split first."""
    def make(box):
        axis, spread = widest(box)
        return {"pixels": box, "axis": axis, "spread": spread}

    boxes = [make(pixels)]
    while len(boxes) < count:
        splittable = [box for box in boxes if len(box["pixels"]) > 1 and box["spread"] > 0]
        if not splittable:
            break
        box = max(splittable, key=lambda item: item["spread"] * math.sqrt(len(item["pixels"])))
        boxes.remove(box)
        ordered = sorted(box["pixels"], key=lambda pixel: pixel[box["axis"]])
        middle = len(ordered) // 2
        boxes.extend([make(ordered[:middle]), make(ordered[middle:])])
    return [(tuple(round(sum(pixel[axis] for pixel in box["pixels"]) / len(box["pixels"])) for axis in range(3)), len(box["pixels"]))
            for box in boxes]


def merge(colors):
    merged = []
    for rgb, count in sorted(colors, key=lambda item: -item[1]):
        for entry in merged:
            if max(abs(a - b) for a, b in zip(entry[0], rgb)) <= SAME_COLOR:
                total = entry[1] + count
                entry[0] = tuple(round((a * entry[1] + b * count) / total) for a, b in zip(entry[0], rgb))
                entry[1] = total
                break
        else:
            merged.append([rgb, count])
    return sorted(merged, key=lambda item: -item[1])


def describe(rgb, count, total, existing):
    hue, saturation, value = hsv(rgb)
    kind = "accent" if saturation >= 0.45 and value >= 0.45 else "light" if value >= 0.85 and saturation <= 0.2 else "surface"
    entry = {"hex": hex_color(rgb), "share": round(count / total, 4), "kind": kind,
             "hsv": [round(hue * 360), round(saturation, 2), round(value, 2)],
             "linear": [round(to_linear(channel), 4) for channel in rgb] + [1.0]}
    if existing:
        closest = min(existing, key=lambda color: max(abs(a - b) for a, b in zip(hex_rgb(color["hex"]), rgb)))
        entry["closest"] = {"hex": closest["hex"], "name": closest["name"], "theme": closest.get("theme") or [],
                            "distance": max(abs(a - b) for a, b in zip(hex_rgb(closest["hex"]), rgb))}
    return entry


STYLE = """
body { margin: 0; padding: 24px; background: #10141b; color: #e8edf3; font: 14px/1.5 "Segoe UI", "Malgun Gothic", sans-serif; }
h1 { margin: 0 0 4px; font-size: 22px; } h2 { margin: 26px 0 10px; font-size: 16px; } .muted { color: #8d9aab; }
.top { display: flex; flex-wrap: wrap; gap: 20px; align-items: flex-start; } .top img { max-width: min(520px, 100%); border-radius: 8px; }
.strip { display: flex; height: 40px; border-radius: 8px; overflow: hidden; margin: 8px 0; min-width: 280px; }
.wrap { overflow-x: auto; } table { border-collapse: collapse; width: 100%; font-size: 13px; }
th, td { text-align: left; vertical-align: middle; padding: 6px 10px 6px 0; border-bottom: 1px solid #222d3b; } th { color: #8d9aab; }
.swatch { display: inline-block; width: 56px; height: 32px; border-radius: 6px; border: 1px solid #2c3847; }
.far { color: #ffcf70; }
"""


def render(summary, image_uri):
    def rows(entries):
        result = []
        for entry in entries:
            closest = entry.get("closest")
            comparison = ""
            if closest:
                theme = ", ".join(closest["theme"]) or "not in the theme"
                style = ' class="far"' if closest["distance"] > 24 else ""
                comparison = (f"<td><span class='swatch' style='background:{closest['hex']}'></span></td>"
                              f"<td{style}>{html.escape(closest['name'])} {closest['hex']}<br>{html.escape(theme)}, off by {closest['distance']}</td>")
            linear = ", ".join(f"{value:.3f}" for value in entry["linear"])
            result.append(f"<tr><td><span class='swatch' style='background:{entry['hex']}'></span></td><td><b>{entry['hex']}</b> {entry['kind']}"
                          f"<div class='muted'>linear {linear}</div></td><td>{entry['share'] * 100:.1f}%</td>{comparison}</tr>")
        return "".join(result)

    headers = "<tr><th></th><th>Color</th><th>Share</th>" + ("<th></th><th>Closest color in the Widget Blueprints</th>" if summary["compare"] else "") + "</tr>"
    strip = "".join(f"<div style='flex:{max(entry['share'], 0.004)};background:{entry['hex']}' title='{entry['hex']}'></div>"
                    for entry in summary["palette"])
    preview = f"<img src='{image_uri}' alt=''>" if image_uri else ""
    accents = (f"<h2>Accents</h2><p class='muted'>Saturated, bright pixels on their own, because small accents vanish from a palette by "
               f"area.</p><div class='wrap'><table>{headers}{rows(summary['accents'])}</table></div>") if summary["accents"] else ""
    return (f"<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
            f"<title>Palette of {html.escape(os.path.basename(summary['image']))}</title><style>{STYLE}</style></head><body>"
            f"<h1>Palette of {html.escape(os.path.basename(summary['image']))}</h1>"
            f"<div class='muted'>{summary['width']}x{summary['height']}, {summary['samples']} pixels sampled - {summary['generated']}"
            f"{' - compared with ' + html.escape(summary['compare']) if summary['compare'] else ''}</div>"
            f"<div class='top'>{preview}<div style='flex:1'><div class='strip'>{strip}</div>"
            f"<p class='muted'>Surfaces are dark or muted colors that cover area, lights are near white, accents are saturated. Line "
            f"widths, radii and spacing are judged next to the style frame in the kit gallery.</p></div></div>"
            f"<h2>Palette</h2><div class='wrap'><table>{headers}{rows(summary['palette'])}</table></div>{accents}</body></html>\n")


def main():
    parser = argparse.ArgumentParser(description="Extracts the palette of a style frame image.")
    parser.add_argument("image", help="PNG file of the style frame")
    parser.add_argument("--colors", type=int, default=10, help="number of main colors")
    parser.add_argument("--compare", help="JSON report of style_extract.py to compare with")
    parser.add_argument("--name", help="file name of the report without extension; default: the image name with _palette")
    parser.add_argument("--out", default=os.path.join("Saved", "UiStyle"), help="folder of the report")
    args = parser.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")

    try:
        width, height, rgba = read_png(args.image)
        existing = None
        if args.compare:
            with open(args.compare, encoding="utf-8-sig") as handle:
                existing = json.load(handle).get("colors") or []
    except (OSError, ValueError) as error:
        print(f"Cannot read the input: {error}", file=sys.stderr)
        return 2

    step = max(1, int(math.sqrt(width * height / SAMPLES)))
    pixels = []
    for y in range(0, height, step):
        base = y * width
        for x in range(0, width, step):
            index = (base + x) * 4
            if rgba[index + 3] >= 128:
                pixels.append((rgba[index], rgba[index + 1], rgba[index + 2]))
    if not pixels:
        print(f"Cannot read a palette: {args.image} has no opaque pixels.", file=sys.stderr)
        return 2

    total = len(pixels)
    palette = [describe(tuple(rgb), count, total, existing) for rgb, count in merge(median_cut(pixels, args.colors * 2))[:args.colors]]
    bright = [pixel for pixel in pixels if (lambda color: color[1] >= 0.45 and color[2] >= 0.55)(hsv(pixel))]
    accents = []
    if len(bright) >= max(20, total // 500):
        accents = [describe(tuple(rgb), count, total, existing) for rgb, count in merge(median_cut(bright, 12))[:6]]

    summary = {"generated": datetime.datetime.now().isoformat(timespec="seconds"), "image": os.path.abspath(args.image), "width": width,
               "height": height, "samples": total, "compare": args.compare, "palette": palette, "accents": accents}
    name = args.name or os.path.splitext(os.path.basename(args.image))[0] + "_palette"
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    image_uri = None
    if os.path.getsize(args.image) <= 8 * 1024 * 1024:
        with open(args.image, "rb") as handle:
            image_uri = "data:image/png;base64," + base64.b64encode(handle.read()).decode("ascii")
    with open(os.path.join(out, name + ".json"), "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2, ensure_ascii=False)
    with open(os.path.join(out, name + ".html"), "w", encoding="utf-8") as handle:
        handle.write(render(summary, image_uri))

    def line(entry):
        closest = entry.get("closest")
        suffix = f" (closest {closest['name']} {closest['hex']}, off by {closest['distance']})" if closest else ""
        return f"{entry['hex']} {entry['kind']} {entry['share'] * 100:.1f}%{suffix}"

    print(f"{os.path.basename(args.image)}: {width}x{height}, {total} pixels sampled")
    print("Palette: " + "; ".join(line(entry) for entry in palette))
    print("Accents: " + ("; ".join(line(entry) for entry in accents) if accents else "none"))
    print(f"Report: {os.path.join(out, name + '.html')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
