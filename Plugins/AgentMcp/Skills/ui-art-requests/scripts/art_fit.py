#!/usr/bin/env python3
"""Fits an image to an item of a UI art request (standard library only).

Usage:
  python art_fit.py SOURCE.png Art/Requests/<feature>.json ITEM_ID [--project FOLDER] [--out FILE]

Writes Art/Incoming/<feature>/<id>.png of the project, an 8-bit RGBA PNG of exactly the size of the item:
- A 9-slice item (a frame or panel) is cropped to its visible pixels and stretched to the size, less its padding when it has one, so
  that its border reaches the edge of the widget. Slate draws 9-slice margins at the texture's pixel size, so a transparent margin
  in the image becomes empty space inside the widget.
- An item with padding, such as an icon, is cropped to its visible pixels, scaled to fit inside the size less the padding on every
  side, and centered on a transparent canvas.
- Any other item is scaled as a whole to the size.

Reductions average the covered source pixels and enlargements interpolate linearly, both with premultiplied alpha, so that the colors
of transparent pixels leave no fringe. Reads 8-bit PNG files without interlacing: gray, RGB, palette, gray with alpha and RGBA.
The request is not changed: record how the image was made in <id>.json, check it with art_review.py, and then set the status.

Exit code: 0, 1 when the image cannot be fitted, or 2 when the request or the item cannot be used.
"""

import argparse
import json
import math
import os
import struct
import sys
import zlib
from array import array

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
# Pixels with this alpha or less do not count as content when an image is cropped, so that faint noise does not shift it.
VISIBLE_ALPHA = 8


def png_chunks(data):
    position = len(PNG_SIGNATURE)
    while position + 8 <= len(data):
        length = struct.unpack(">I", data[position:position + 4])[0]
        kind = data[position + 4:position + 8]
        yield kind, data[position + 8:position + 8 + length]
        if kind == b"IEND":
            return
        position += 12 + length


def unfilter(raw, width, height, channels):
    stride = width * channels
    if len(raw) < height * (stride + 1):
        raise ValueError("the image data is damaged")
    samples = bytearray(height * stride)
    previous = bytearray(stride)
    offset = 0
    for y in range(height):
        filter_type = raw[offset]
        line = bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride
        if filter_type == 1:
            for x in range(channels, stride):
                line[x] = (line[x] + line[x - channels]) & 0xFF
        elif filter_type == 2:
            for x in range(stride):
                line[x] = (line[x] + previous[x]) & 0xFF
        elif filter_type == 3:
            for x in range(stride):
                left = line[x - channels] if x >= channels else 0
                line[x] = (line[x] + ((left + previous[x]) >> 1)) & 0xFF
        elif filter_type == 4:
            for x in range(stride):
                up = previous[x]
                left, up_left = (line[x - channels], previous[x - channels]) if x >= channels else (0, 0)
                estimate = left + up - up_left
                distance_left, distance_up, distance_up_left = abs(estimate - left), abs(estimate - up), abs(estimate - up_left)
                if distance_left <= distance_up and distance_left <= distance_up_left:
                    predictor = left
                elif distance_up <= distance_up_left:
                    predictor = up
                else:
                    predictor = up_left
                line[x] = (line[x] + predictor) & 0xFF
        elif filter_type != 0:
            raise ValueError("the image data is damaged")
        samples[y * stride:(y + 1) * stride] = line
        previous = line
    return samples


def read_png(path):
    """(width, height, RGBA pixels, whether the file has transparency) of an 8-bit PNG file without interlacing."""
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 33 or not data.startswith(PNG_SIGNATURE) or data[12:16] != b"IHDR":
        raise ValueError("it is not a PNG file")
    width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", data[16:29])
    if bit_depth != 8 or interlace != 0 or color_type not in CHANNELS:
        raise ValueError(f"it is a PNG with bit depth {bit_depth}, color type {color_type} and interlace {interlace}; "
                         "save it as an 8-bit PNG without interlacing")
    palette, transparency, compressed = b"", b"", []
    for kind, payload in png_chunks(data):
        if kind == b"PLTE":
            palette = payload
        elif kind == b"tRNS":
            transparency = payload
        elif kind == b"IDAT":
            compressed.append(payload)
    try:
        samples = unfilter(zlib.decompress(b"".join(compressed)), width, height, CHANNELS[color_type])
    except zlib.error:
        raise ValueError("the image data is damaged") from None

    count = width * height
    if color_type == 6:
        return width, height, samples, True
    rgba = bytearray(count * 4)
    rgba[3::4] = b"\xff" * count
    if color_type == 2:
        rgba[0::4], rgba[1::4], rgba[2::4] = samples[0::3], samples[1::3], samples[2::3]
        if len(transparency) == 6:
            key = bytes((transparency[1], transparency[3], transparency[5]))
            for index in range(count):
                if samples[index * 3:index * 3 + 3] == key:
                    rgba[index * 4 + 3] = 0
    elif color_type in (0, 4):
        gray = samples[0::CHANNELS[color_type]]
        rgba[0::4] = rgba[1::4] = rgba[2::4] = gray
        if color_type == 4:
            rgba[3::4] = samples[1::2]
        elif len(transparency) == 2:
            for index in range(count):
                if samples[index] == transparency[1]:
                    rgba[index * 4 + 3] = 0
    else:
        if not palette:
            raise ValueError("the palette PNG has no palette")
        colors = []
        for index in range(256):
            rgb = palette[index * 3:index * 3 + 3] if index * 3 + 3 <= len(palette) else b"\x00\x00\x00"
            colors.append(bytes(rgb) + bytes((transparency[index] if index < len(transparency) else 255,)))
        rgba = bytearray(b"".join(colors[index] for index in samples))
    return width, height, rgba, color_type == 4 or bool(transparency)


def write_png(path, width, height, rgba):
    stride = width * 4
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    with open(path, "wb") as handle:
        handle.write(PNG_SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def visible_bounds(rgba, width, height):
    """(left, top, right, bottom) exclusive bounds of the pixels with alpha above VISIBLE_ALPHA, or None."""
    alphas = rgba[3::4]
    left, top, right, bottom = width, height, 0, 0
    for y in range(height):
        row = alphas[y * width:(y + 1) * width]
        if max(row) <= VISIBLE_ALPHA:
            continue
        first = next(x for x, alpha in enumerate(row) if alpha > VISIBLE_ALPHA)
        last = width - next(x for x, alpha in enumerate(reversed(row)) if alpha > VISIBLE_ALPHA)
        left, right = min(left, first), max(right, last)
        top, bottom = min(top, y), y + 1
    return (left, top, right, bottom) if right > left else None


def axis_weights(start, length, target):
    """For every target pixel, the (source pixel, weight) pairs over the source pixels start to start + length - 1."""
    scale = length / target
    weights = []
    for index in range(target):
        if scale >= 1:
            begin = start + index * scale
            end = begin + scale
            pairs = [(source, min(end, source + 1) - max(begin, source))
                     for source in range(int(begin), min(math.ceil(end), start + length))]
            pairs = [(source, overlap) for source, overlap in pairs if overlap > 1e-9]
        else:
            center = start + (index + 0.5) * scale - 0.5
            first = math.floor(center)
            fraction = center - first
            clamp = lambda value: min(max(value, start), start + length - 1)
            pairs = [(clamp(first), 1 - fraction), (clamp(first + 1), fraction)]
        total = sum(weight for _, weight in pairs)
        weights.append([(source, weight / total) for source, weight in pairs])
    return weights


def resample(rgba, width, box, target_width, target_height):
    """RGBA pixels of the source box (left, top, right, bottom, exclusive) scaled to target_width x target_height."""
    left, top, right, bottom = box
    columns = axis_weights(left, right - left, target_width)
    rows = axis_weights(top, bottom - top, target_height)
    horizontal = {}
    for y in sorted({source for pairs in rows for source, _ in pairs}):
        base = y * width * 4
        line = array("d", bytes(8 * target_width * 4))
        for x, pairs in enumerate(columns):
            red = green = blue = alpha = 0.0
            for source, weight in pairs:
                index = base + source * 4
                covered = rgba[index + 3] * weight
                red += rgba[index] * covered
                green += rgba[index + 1] * covered
                blue += rgba[index + 2] * covered
                alpha += covered
            line[x * 4:x * 4 + 4] = array("d", (red, green, blue, alpha))
        horizontal[y] = line

    result = bytearray(target_width * target_height * 4)
    for y, pairs in enumerate(rows):
        lines = [(horizontal[source], weight) for source, weight in pairs]
        for x in range(target_width):
            index = x * 4
            red = green = blue = alpha = 0.0
            for line, weight in lines:
                red += line[index] * weight
                green += line[index + 1] * weight
                blue += line[index + 2] * weight
                alpha += line[index + 3] * weight
            if alpha > 0:
                output = (y * target_width + x) * 4
                result[output:output + 4] = bytes((min(255, round(red / alpha)), min(255, round(green / alpha)),
                                                   min(255, round(blue / alpha)), min(255, round(alpha))))
    return result


def main():
    parser = argparse.ArgumentParser(description="Fits an image to an item of a UI art request.")
    parser.add_argument("source", help="PNG file from an image model, another tool or an artist")
    parser.add_argument("request", help="path of Art/Requests/<feature>.json")
    parser.add_argument("item", help="id of the item")
    parser.add_argument("--project", help="project folder; default: the folder that contains Art/Requests")
    parser.add_argument("--out", help="PNG file to write; default: Art/Incoming/<feature>/<id>.png of the project")
    args = parser.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")

    request_path = os.path.abspath(args.request)
    try:
        # utf-8-sig also reads files that Windows tools wrote with a byte order mark.
        with open(request_path, encoding="utf-8-sig") as handle:
            request = json.load(handle)
    except (OSError, ValueError) as error:
        print(f"Cannot read the request {request_path}: {error}", file=sys.stderr)
        return 2
    items = request.get("items") if isinstance(request, dict) else None
    item = next((entry for entry in items or [] if isinstance(entry, dict) and entry.get("id") == args.item), None)
    if item is None:
        print(f"The request {request_path} has no item {args.item}.", file=sys.stderr)
        return 2
    size = item.get("size")
    if not (isinstance(size, list) and len(size) == 2 and all(isinstance(value, int) and not isinstance(value, bool) and value > 0 for value in size)):
        print(f"The item {args.item} has no size [width, height] in whole pixels.", file=sys.stderr)
        return 2
    target_width, target_height = size
    padding = item.get("padding")
    padded = isinstance(padding, int) and not isinstance(padding, bool) and padding >= 0
    if padded and (2 * padding >= target_width or 2 * padding >= target_height):
        print(f"The padding of the item {args.item} leaves no room in {target_width}x{target_height}.", file=sys.stderr)
        return 2
    nine_slice = item.get("nineSlice")
    sliced = isinstance(nine_slice, list) and len(nine_slice) == 4 and all(
        isinstance(value, (int, float)) and not isinstance(value, bool) for value in nine_slice)

    feature = str(request.get("feature") or os.path.splitext(os.path.basename(request_path))[0])
    project = os.path.abspath(args.project) if args.project else os.path.dirname(os.path.dirname(os.path.dirname(request_path)))
    out = os.path.abspath(args.out) if args.out else os.path.join(project, "Art", "Incoming", feature, args.item + ".png")

    try:
        width, height, rgba, has_alpha = read_png(args.source)
    except (OSError, ValueError) as error:
        print(f"Cannot fit {args.source}: {error}", file=sys.stderr)
        return 1
    if item.get("transparent") and not has_alpha:
        print(f"Cannot fit {args.source}: the item is transparent, but the image has no transparency. Make it with a transparent background.",
              file=sys.stderr)
        return 1

    notes = []
    if sliced or padded:
        box = visible_bounds(rgba, width, height)
        if box is None:
            print(f"Cannot fit {args.source}: it has no visible pixels.", file=sys.stderr)
            return 1
        left, top, right, bottom = box
        content_width, content_height = right - left, bottom - top
    if sliced:
        margin = padding if padded else 0
        fitted_width, fitted_height = target_width - 2 * margin, target_height - 2 * margin
        offset_x = offset_y = margin
        done = (f"cropped to {content_width}x{content_height} at {left},{top} and stretched to {fitted_width}x{fitted_height}"
                + (f" inside {margin} px padding" if margin else ", so that its border reaches the edge"))
        enlarged = max(fitted_width / content_width, fitted_height / content_height)
        if abs((content_width / content_height) / (fitted_width / fitted_height) - 1) > 0.02:
            notes.append(f"the proportions change from {content_width}x{content_height} to {fitted_width}x{fitted_height}")
        scale_x, scale_y = content_width / fitted_width, content_height / fitted_height
        source_border = [round(max(0, nine_slice[0] - margin) * scale_x), round(max(0, nine_slice[1] - margin) * scale_y),
                         round(max(0, nine_slice[2] - margin) * scale_x), round(max(0, nine_slice[3] - margin) * scale_y)]
        notes.append(f"the 9-slice border {nine_slice} is {source_border} px of the cropped source; its ornament must end inside it")
    elif padded:
        room_width, room_height = target_width - 2 * padding, target_height - 2 * padding
        scale = min(room_width / content_width, room_height / content_height)
        fitted_width = max(1, min(room_width, math.floor(content_width * scale + 1e-6)))
        fitted_height = max(1, min(room_height, math.floor(content_height * scale + 1e-6)))
        offset_x, offset_y = (target_width - fitted_width) // 2, (target_height - fitted_height) // 2
        done = (f"cropped to {content_width}x{content_height} at {left},{top}, scaled to {fitted_width}x{fitted_height} "
                f"and centered with {padding} px padding")
        enlarged = scale
    else:
        box = (0, 0, width, height)
        fitted_width, fitted_height, offset_x, offset_y = target_width, target_height, 0, 0
        done = "scaled as a whole"
        enlarged = max(target_width / width, target_height / height)
        if abs((width / height) / (target_width / target_height) - 1) > 0.01:
            notes.append(f"the proportions change from {width}x{height} to {target_width}x{target_height}")
    if enlarged > 1.01:
        notes.append(f"the image is enlarged {enlarged:.2f} times and may look soft; a larger source is better")

    content = resample(rgba, width, box, fitted_width, fitted_height)
    canvas = bytearray(target_width * target_height * 4)
    for y in range(fitted_height):
        start = ((offset_y + y) * target_width + offset_x) * 4
        canvas[start:start + fitted_width * 4] = content[y * fitted_width * 4:(y + 1) * fitted_width * 4]
    os.makedirs(os.path.dirname(out), exist_ok=True)
    write_png(out, target_width, target_height, canvas)

    print(f"{args.item}: {args.source} ({width}x{height}) {done} -> {out} ({target_width}x{target_height})")
    for note in notes:
        print(f"Note: {note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
