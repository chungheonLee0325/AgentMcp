#!/usr/bin/env python3
"""Writes the review sheet of a UI art request and prints the next steps (standard library only).

Usage:
  python art_review.py Art/Requests/<feature>.json [--project FOLDER] [--incoming FOLDER] [--out FILE]

The request format is described in the ui-art-requests skill. Delivered images are read from Art/Incoming/<feature>/<id>.png of the
project folder, which is the folder that contains Art/Requests unless --project says otherwise. The sheet is one HTML file with the
images embedded, written to Saved/ArtReview/<feature>.html of the project by default.

Checks of the request: a known status, a unique id and target, a size, and a 9-slice border that leaves a middle to stretch. Checks of
each image: a PNG of the requested size, an alpha channel when the item is transparent, the requested padding around an icon, and a
record <id>.json next to it.

Exit code: 1 when the request has an error or an item marked delivered, imported or approved has an image error, 2 when the request
cannot be read, otherwise 0.
"""

import argparse
import base64
import datetime
import html
import json
import os
import struct
import sys
import zlib

STATUSES = ["requested", "delivered", "imported", "approved", "revise"]
DELIVERED = ("delivered", "imported", "approved")
USAGES = ["icon", "frame", "panel", "decoration", "background"]
ID_CHARACTERS = set("abcdefghijklmnopqrstuvwxyz0123456789_")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png_chunks(data):
    position = len(PNG_SIGNATURE)
    while position + 8 <= len(data):
        length = struct.unpack(">I", data[position:position + 4])[0]
        kind = data[position + 4:position + 8]
        yield kind, data[position + 8:position + 8 + length]
        if kind == b"IEND":
            return
        position += 12 + length


def paeth(left, up, up_left):
    estimate = left + up - up_left
    distance_left, distance_up, distance_up_left = abs(estimate - left), abs(estimate - up), abs(estimate - up_left)
    if distance_left <= distance_up and distance_left <= distance_up_left:
        return left
    return up if distance_up <= distance_up_left else up_left


def rgba_alpha_bounds(data, width, height):
    """Bounds (left, top, right, bottom) of the pixels with alpha > 0 of an 8-bit RGBA PNG, or None when every pixel is transparent."""
    compressed = b"".join(payload for kind, payload in png_chunks(data) if kind == b"IDAT")
    raw = zlib.decompress(compressed)
    stride = width * 4
    previous = bytearray(stride)
    left, top, right, bottom = width, height, -1, -1
    offset = 0
    for y in range(height):
        filter_type = raw[offset]
        if filter_type > 4:
            raise ValueError(f"unknown PNG filter {filter_type}")
        line = bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride
        for x in range(stride):
            a = line[x - 4] if x >= 4 else 0
            b = previous[x]
            c = previous[x - 4] if x >= 4 else 0
            if filter_type == 1:
                line[x] = (line[x] + a) & 0xFF
            elif filter_type == 2:
                line[x] = (line[x] + b) & 0xFF
            elif filter_type == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 0xFF
            elif filter_type == 4:
                line[x] = (line[x] + paeth(a, b, c)) & 0xFF
        for x in range(width):
            if line[x * 4 + 3]:
                left, right = min(left, x), max(right, x)
                top, bottom = min(top, y), max(bottom, y)
        previous = line
    return None if right < 0 else (left, top, right, bottom)


def items_text(count):
    return f"{count} item" if count == 1 else f"{count} items"


def is_pixels(value, minimum=0):
    return isinstance(value, int) and not isinstance(value, bool) and value >= minimum


def valid_size(item):
    size = item.get("size")
    return size if isinstance(size, list) and len(size) == 2 and all(is_pixels(value, 1) for value in size) else None


def inspect_image(path, item):
    """Problems of a delivered image as (level, text) pairs, where level is error, warning or note."""
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 33 or not data.startswith(PNG_SIGNATURE) or data[12:16] != b"IHDR":
        return [("error", "the file is not a PNG")]
    problems = []
    width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", data[16:29])
    has_alpha = color_type in (4, 6) or any(kind == b"tRNS" for kind, _ in png_chunks(data))

    size = valid_size(item)
    if size and [width, height] != size:
        problems.append(("error", f"the image is {width}x{height}, the request asks for {size[0]}x{size[1]}"))
    if not item.get("transparent"):
        return problems
    if not has_alpha:
        problems.append(("error", "the image has no alpha channel, but the item is transparent"))
        return problems
    if color_type != 6 or bit_depth != 8 or interlace != 0:
        problems.append(("note", "transparency and padding were not measured (only 8-bit RGBA PNG files without interlacing are)"))
        return problems
    try:
        bounds = rgba_alpha_bounds(data, width, height)
    except (zlib.error, IndexError, ValueError):
        problems.append(("error", "the image data is damaged"))
        return problems
    if bounds is None:
        problems.append(("error", "every pixel is transparent"))
        return problems
    padding = item.get("padding")
    if item.get("usage") == "icon" and is_pixels(padding):
        left, top, right, bottom = bounds
        margin = min(left, top, width - 1 - right, height - 1 - bottom)
        if margin < padding:
            problems.append(("warning", f"the content comes within {margin} px of the edge; the request asks for {padding} px of padding"))
    return problems


def check_request_item(item, seen_ids, seen_targets):
    """Problems of the request entry of one item as (level, text) pairs."""
    problems = []
    item_id = item.get("id")
    if not isinstance(item_id, str) or not item_id:
        problems.append(("error", "the item has no id"))
    else:
        if item_id in seen_ids:
            problems.append(("error", f"another item has the id {item_id}"))
        elif not set(item_id) <= ID_CHARACTERS:
            problems.append(("warning", "the id should be snake_case: lowercase letters, digits and underscores"))
        seen_ids.add(item_id)

    status = item.get("status")
    if status not in STATUSES:
        problems.append(("error", f"unknown status {json.dumps(status)} (use {', '.join(STATUSES)})"))
    elif status == "revise" and not item.get("feedback"):
        problems.append(("warning", "the item is to be revised, but has no feedback"))

    if not item.get("size"):
        problems.append(("error", "the request has no size"))
    elif not valid_size(item):
        problems.append(("error", "size must be [width, height] in whole pixels"))
    for field in ("usage", "subject", "target", "usedBy"):
        if not item.get(field):
            problems.append(("warning", f"the request has no {field}"))
    usage = item.get("usage")
    if usage and usage not in USAGES:
        problems.append(("warning", f"unknown usage {json.dumps(usage)} (use {', '.join(USAGES)})"))

    target = item.get("target")
    if isinstance(target, str) and target:
        if target in seen_targets:
            problems.append(("error", f"another item has the target {target}"))
        seen_targets.add(target)

    nine_slice = item.get("nineSlice")
    size = valid_size(item)
    if nine_slice is not None:
        if not (isinstance(nine_slice, list) and len(nine_slice) == 4 and all(is_pixels(value) for value in nine_slice)):
            problems.append(("error", "nineSlice must be [left, top, right, bottom] in whole pixels"))
        elif size and (nine_slice[0] + nine_slice[2] >= size[0] or nine_slice[1] + nine_slice[3] >= size[1]):
            problems.append(("error", "the 9-slice border leaves no middle to stretch"))
    return problems


def check_image(item, incoming):
    """(image path or None, problems) of the delivered image of one item."""
    item_id = item.get("id")
    if not isinstance(item_id, str) or not item_id:
        return None, []
    status = item.get("status")
    image = os.path.join(incoming, item_id + ".png")
    if not os.path.isfile(image):
        if status in DELIVERED:
            return None, [("error", f"{item_id}.png is missing in {incoming}")]
        return None, []

    problems = inspect_image(image, item)
    if status in ("requested", "revise"):
        problems.append(("note", f"an image is there, but the status is still {status}"))
    if not os.path.isfile(os.path.join(incoming, item_id + ".json")):
        problems.append(("warning", f"there is no record {item_id}.json next to the image"))
    return image, problems


def next_steps(feature, counts):
    """(role, action) pairs of every role that has items to work on, in the order of the workflow."""
    sentence = f"\"Continue the art request {feature}.\""
    steps = []
    waiting = counts.get("requested", 0) + counts.get("revise", 0)
    if waiting:
        steps.append(("Image maker", f"Make or remake the images of {items_text(waiting)}. "
                                     f"Tell the image maker (an agent that can generate images, or an artist): {sentence}"))
    if counts.get("delivered"):
        steps.append(("UI agent", f"Import and connect the images of {items_text(counts['delivered'])}. Tell the UI agent: {sentence}"))
    if counts.get("imported"):
        steps.append(("Reviewer", f"Check {items_text(counts['imported'])} in this sheet and in the game, then tell the UI agent which "
                                  "are approved and what to change in the others."))
    if not steps:
        if not counts:
            steps.append(("UI agent", "Add the items to the request."))
        elif set(counts) == {"approved"}:
            steps.append(("Nobody", "Every item is approved."))
        else:
            steps.append(("UI agent", "Fix the statuses that the checks report."))
    return steps


def data_uri(path):
    extension = os.path.splitext(path)[1].lower()
    mime = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg"}.get(extension)
    if not mime or not os.path.isfile(path) or os.path.getsize(path) > 8 * 1024 * 1024:
        return None
    with open(path, "rb") as handle:
        return f"data:{mime};base64," + base64.b64encode(handle.read()).decode("ascii")


STYLE = """
body { margin: 0; padding: 24px; background: #0e1520; color: #e6edf5; font: 14px/1.5 "Segoe UI", "Malgun Gothic", sans-serif; }
h1 { margin: 0 0 4px; font-size: 22px; } h2 { font-size: 16px; margin: 28px 0 12px; }
.muted, figcaption { color: #8fa3b8; } .box { background: #162131; border: 1px solid #25354a; border-radius: 10px; padding: 14px 16px; }
.next { margin-top: 16px; border-color: #3d8bfd; } .next ul { margin: 4px 0 10px; padding-left: 18px; }
.counts span { display: inline-block; margin: 4px 8px 0 0; padding: 2px 10px; border-radius: 999px; background: #22324a; }
.grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(min(330px, 100%), 1fr)); gap: 16px; }
.wide { grid-column: 1 / -1; }
.item h3 { margin: 0; font-size: 15px; display: flex; justify-content: space-between; gap: 8px; }
.badge { font-size: 12px; padding: 1px 8px; border-radius: 999px; background: #33445c; }
.badge.requested { background: #4a4f5c; } .badge.delivered { background: #2d5f9e; } .badge.imported { background: #7a5a16; }
.badge.approved { background: #2f7a45; } .badge.revise { background: #8f2f2f; }
table { border-collapse: collapse; width: 100%; margin: 8px 0; font-size: 12px; } td { padding: 2px 6px 2px 0; vertical-align: top; word-break: break-all; }
td:first-child { color: #8fa3b8; width: 76px; word-break: normal; }
.previews { display: flex; flex-wrap: wrap; gap: 10px; align-items: flex-end; margin: 10px 0; overflow-x: auto; }
figure { margin: 0; font-size: 11px; text-align: center; }
.checker { background-color: #fff; background-image: linear-gradient(45deg, #ccc 25%, transparent 25%), linear-gradient(-45deg, #ccc 25%, transparent 25%),
  linear-gradient(45deg, transparent 75%, #ccc 75%), linear-gradient(-45deg, transparent 75%, #ccc 75%);
  background-size: 16px 16px; background-position: 0 0, 0 8px, 8px -8px, -8px 0; }
.panel { background: #0b1522; padding: 8px; border-radius: 8px; }
.previews img { display: block; }
.stretch { box-sizing: border-box; }
.reference img { max-width: min(320px, 100%); border-radius: 8px; } .capture { margin-bottom: 12px; } .capture img { max-width: 100%; border-radius: 8px; }
ul.checks { margin: 6px 0 0; padding-left: 18px; font-size: 12px; } .error { color: #ff8a80; } .warning { color: #ffd27a; } .note { color: #8fa3b8; } .ok { color: #7ee2a0; }
.feedback { margin-top: 8px; padding: 6px 8px; border-left: 3px solid #ff8a80; background: #241a1d; font-size: 12px; }
"""


def render_item(item, image, problems):
    item_id = html.escape(str(item.get("id") or "(no id)"))
    status = html.escape(str(item.get("status") or ""))
    rows = []
    for field in ("usage", "subject", "size", "transparent", "padding", "nineSlice", "target", "usedBy"):
        if field in item:
            value = item[field]
            if field == "size" and isinstance(value, list):
                text = "x".join(str(part) for part in value)
            elif isinstance(value, (list, dict, bool)):
                text = json.dumps(value, ensure_ascii=False)
            else:
                text = str(value)
            rows.append(f"<tr><td>{field}</td><td>{html.escape(text)}</td></tr>")

    size = valid_size(item)
    previews = ""
    uri = data_uri(image) if image else None
    if uri:
        parts = [f'<figure><img class="checker" src="{uri}" alt=""><figcaption>actual size</figcaption></figure>',
                 f'<figure class="panel"><img src="{uri}" alt=""><figcaption>on the panel color</figcaption></figure>']
        nine_slice = item.get("nineSlice")
        if size and isinstance(nine_slice, list) and len(nine_slice) == 4 and all(is_pixels(value) for value in nine_slice):
            left, top, right, bottom = nine_slice
            width = max(round(size[0] * 1.6), left + right + 16)
            height = max(round(size[1] * 0.8), top + bottom + 16)
            parts.append(f'<figure class="panel"><div class="stretch" style="width:{width}px;height:{height}px;border-style:solid;'
                         f'border-width:{top}px {right}px {bottom}px {left}px;border-image:url({uri}) {top} {right} {bottom} {left} fill stretch;"></div>'
                         f'<figcaption>9-slice stretched to {width}x{height}</figcaption></figure>')
        previews = '<div class="previews">' + "".join(parts) + "</div>"

    checks = list(problems)
    if image and not any(level in ("error", "warning") for level, _ in problems):
        checks.insert(0, ("ok", "the image passes the automatic checks"))
    if not image and item.get("status") in ("requested", "revise"):
        checks.append(("note", "no image yet"))
    check_list = "".join(f'<li class="{level}">{html.escape(text)}</li>' for level, text in checks)
    feedback = f'<div class="feedback">{html.escape(str(item["feedback"]))}</div>' if item.get("feedback") else ""
    wide = " wide" if size and size[0] > 280 else ""
    return (f'<div class="box item{wide}"><h3><span>{item_id}</span><span class="badge {status}">{status}</span></h3>'
            f'<table>{"".join(rows)}</table>{previews}<ul class="checks">{check_list}</ul>{feedback}</div>')


def render_files(paths, project, css_class):
    parts = []
    for path in paths if isinstance(paths, list) else []:
        if not isinstance(path, str):
            continue
        full = path if os.path.isabs(path) else os.path.join(project, path)
        uri = data_uri(full)
        if uri:
            parts.append(f'<figure class="{css_class}"><img src="{uri}" alt=""><figcaption>{html.escape(path)}</figcaption></figure>')
        else:
            reason = "not shown (only PNG and JPEG files up to 8 MB are)" if os.path.isfile(full) else "not found"
            parts.append(f'<div class="note">{html.escape(path)}: {reason}</div>')
    return "".join(parts)


def main():
    parser = argparse.ArgumentParser(description="Writes the review sheet of a UI art request.")
    parser.add_argument("request", help="path of Art/Requests/<feature>.json")
    parser.add_argument("--project", help="project folder; default: the folder that contains Art/Requests")
    parser.add_argument("--incoming", help="folder of the delivered images; default: Art/Incoming/<feature> of the project")
    parser.add_argument("--out", help="HTML file to write; default: Saved/ArtReview/<feature>.html of the project")
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
    if not isinstance(request, dict) or not isinstance(request.get("items"), list):
        print(f"{request_path} is not an art request: it needs an items list.", file=sys.stderr)
        return 2

    feature = str(request.get("feature") or os.path.splitext(os.path.basename(request_path))[0])
    project = os.path.abspath(args.project) if args.project else os.path.dirname(os.path.dirname(os.path.dirname(request_path)))
    incoming = os.path.abspath(args.incoming) if args.incoming else os.path.join(project, "Art", "Incoming", feature)
    out = os.path.abspath(args.out) if args.out else os.path.join(project, "Saved", "ArtReview", feature + ".html")

    counts = {}
    cards = []
    errors = []
    seen_ids, seen_targets = set(), set()
    for index, item in enumerate(request["items"]):
        if not isinstance(item, dict):
            errors.append(f"items[{index}]: not an object")
            continue
        request_problems = check_request_item(item, seen_ids, seen_targets)
        image, image_problems = check_image(item, incoming)
        status = str(item.get("status"))
        counts[status] = counts.get(status, 0) + 1
        name = item.get("id") or f"items[{index}]"
        errors.extend(f"{name}: {text}" for level, text in request_problems if level == "error")
        if status in DELIVERED:
            errors.extend(f"{name}: {text}" for level, text in image_problems if level == "error")
        cards.append(render_item(item, image, request_problems + image_problems))

    counts = dict(sorted(counts.items(), key=lambda entry: STATUSES.index(entry[0]) if entry[0] in STATUSES else len(STATUSES)))
    steps = next_steps(feature, counts)

    step_list = "".join(f"<li><b>{html.escape(role)}</b>: {html.escape(action)}</li>" for role, action in steps)
    error_block = ""
    if errors:
        error_block = '<b class="error">Errors</b><ul>' + "".join(f'<li class="error">{html.escape(text)}</li>' for text in errors) + "</ul>"
    count_text = "".join(f"<span>{html.escape(status)} {count}</span>" for status, count in counts.items())
    references = render_files(request.get("references"), project, "reference")
    reference_block = f'<div class="previews">{references}</div>' if references else ""
    captures = render_files(request.get("captures"), project, "capture")
    capture_block = f"<h2>Captures</h2>{captures}" if captures else ""
    generated = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")

    page = (f'<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
            f"<title>Art request {html.escape(feature)}</title><style>{STYLE}</style></head><body>\n"
            f"<h1>Art request: {html.escape(feature)}</h1>\n"
            f'<div class="muted">{html.escape(str(request.get("description") or ""))}</div>\n'
            f'<div class="box next"><b>Next</b><ul>{step_list}</ul>{error_block}<div class="counts">{count_text}</div></div>\n'
            f'<h2>Style</h2><div class="box">{html.escape(str(request.get("style") or ""))}{reference_block}</div>\n'
            f'<h2>Items</h2><div class="grid">{"".join(cards)}</div>\n'
            f"{capture_block}\n"
            f'<p class="muted">{html.escape(request_path)} - images from {html.escape(incoming)} - {generated}</p>\n'
            f"</body></html>\n")

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as handle:
        handle.write(page)

    print(f"{feature}: {items_text(len(request['items']))} - " + ", ".join(f"{status} {count}" for status, count in counts.items()))
    for role, action in steps:
        print(f"Next: {role} - {action}")
    for text in errors:
        print(f"Error: {text}")
    print(f"Review sheet: {out}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
