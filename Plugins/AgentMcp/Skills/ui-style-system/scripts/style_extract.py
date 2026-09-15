#!/usr/bin/env python3
"""Extracts the style that Widget Blueprints actually use, through the Agent MCP server (standard library only).

Usage:
  python style_extract.py [--url URL] [--token TOKEN] [--path /Game/UI] [--theme OBJECT_PATH] [--name NAME] [--out FOLDER]

Reads every Widget Blueprint under --path with umg_inspect and, with --theme, the theme data asset with object_get_properties. Writes
<out>/<name>.json with the token candidates and <out>/<name>.html with a report: color clusters (sRGB, and the linear values that
Unreal stores) with their uses and the theme token they match, near duplicates, the type scale, corner radii, line widths, spacing and
how much of it fits a 4 and an 8 unit grid, fixed sizes and textures. Values that code or Blueprint graphs set at runtime are not in the
Widget Blueprints; only their placeholders are.

--url defaults to the AGENT_MCP_URL environment variable, then to http://127.0.0.1:18765/mcp. --out defaults to Saved/UiStyle of the
current folder, which should be the project folder.

Exit code: 0, 1 when the server cannot be read, 2 for invalid arguments.
"""

import argparse
import collections
import datetime
import html
import json
import os
import sys
import urllib.error
import urllib.request

TEXT_CLASSES = {"TextBlock", "RichTextBlock", "EditableText", "EditableTextBox", "MultiLineEditableText", "MultiLineEditableTextBox",
                "CommonTextBlock"}
SIZE_KEYS = ("WidthOverride", "HeightOverride", "MinDesiredWidth", "MinDesiredHeight", "MaxDesiredWidth", "MaxDesiredHeight")
# Colors whose sRGB channels differ by at most SAME_COLOR and whose alpha differs by at most SAME_ALPHA count as one color.
SAME_COLOR = 10
SAME_ALPHA = 0.08
# A color used once or twice that is this close to a more frequent one is reported as its near duplicate.
NEAR_COLOR = 28


class McpClient:
    """The MCP Streamable HTTP requests that this script needs."""

    def __init__(self, url, token=None, timeout=120.0):
        self.url = url
        self.token = token
        self.timeout = timeout
        self.session = None
        self.next_id = 1

    def post(self, message):
        request = urllib.request.Request(self.url, data=json.dumps(message).encode("utf-8"), method="POST")
        request.add_header("Content-Type", "application/json")
        request.add_header("Accept", "application/json, text/event-stream")
        if self.token:
            request.add_header("Authorization", "Bearer " + self.token)
        if self.session:
            request.add_header("Mcp-Session-Id", self.session)
        with urllib.request.urlopen(request, timeout=self.timeout) as response:
            return response.headers, response.read().decode("utf-8")

    def request(self, method, params):
        message = {"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params}
        self.next_id += 1
        headers, text = self.post(message)
        return headers, json.loads(text) if text.strip() else {}

    def start(self):
        headers, _ = self.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                                 "clientInfo": {"name": "style_extract.py", "version": "1.0"}})
        self.session = headers.get("Mcp-Session-Id")
        if not self.session:
            raise RuntimeError("the server returned no MCP session")
        self.post({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def call(self, name, arguments):
        _, payload = self.request("tools/call", {"name": name, "arguments": arguments})
        if "error" in payload:
            raise RuntimeError(f"{name}: {payload['error'].get('message')}")
        result = payload.get("result") or {}
        text = "".join(item.get("text", "") for item in result.get("content") or [] if item.get("type") == "text")
        data = json.loads(text) if text else {}
        if result.get("isError"):
            error = data.get("error") if isinstance(data, dict) else None
            raise RuntimeError(f"{name}: {(error or {}).get('message', text)}")
        return data

    def close(self):
        if not self.session:
            return
        request = urllib.request.Request(self.url, method="DELETE")
        request.add_header("Mcp-Session-Id", self.session)
        if self.token:
            request.add_header("Authorization", "Bearer " + self.token)
        try:
            urllib.request.urlopen(request, timeout=10).close()
        except OSError:
            pass


def is_color(value):
    return isinstance(value, dict) and all(isinstance(value.get(key), (int, float)) for key in ("r", "g", "b", "a"))


def to_srgb(channel):
    channel = min(max(float(channel), 0.0), 1.0)
    return round(255 * (12.92 * channel if channel <= 0.0031308 else 1.055 * channel ** (1 / 2.4) - 0.055))


def hex_color(rgb):
    return "#" + "".join(f"{value:02X}" for value in rgb)


def hex_rgb(text):
    return tuple(int(text[index:index + 2], 16) for index in (1, 3, 5))


def number_text(value):
    return str(int(value)) if float(value).is_integer() else f"{value:g}"


def distance(first, second):
    return max(abs(a - b) for a, b in zip(first, second))


class Record:
    """Style values found in Widget Blueprints, each with the widget property it came from."""

    def __init__(self):
        self.blueprints = []
        self.widget_count = 0
        self.colors = []
        self.fonts = collections.defaultdict(list)
        self.radii = collections.defaultdict(list)
        self.widths = collections.defaultdict(list)
        self.spacing = collections.defaultdict(list)
        self.sizes = collections.defaultdict(list)
        self.textures = collections.defaultdict(list)

    def add_tree(self, blueprint, tree):
        short = blueprint.rsplit("/", 1)[-1].split(".")[0]
        self.blueprints.append(blueprint)
        for widget in tree.get("widgets") or []:
            self.widget_count += 1
            owner = f"{short}.{widget.get('name')}"
            class_name = widget.get("className") or ""
            properties = widget.get("properties") or {}
            for key in SIZE_KEYS:
                if isinstance(properties.get(key), (int, float)) and properties.get("bOverride_" + key):
                    self.sizes[(key, properties[key])].append(owner)
            entry_spacing = properties.get("EntrySpacing")
            if isinstance(entry_spacing, dict):
                for axis in ("x", "y"):
                    if isinstance(entry_spacing.get(axis), (int, float)) and entry_spacing[axis]:
                        self.spacing[entry_spacing[axis]].append(owner + ".EntrySpacing")
            for key, value in properties.items():
                self.collect(value, [key], owner, class_name)
            slot = widget.get("slot") or {}
            # A Border's Padding is its content slot's padding, which the parent already reports.
            if slot.get("className") != "BorderSlot":
                for key, value in (slot.get("properties") or {}).items():
                    self.collect(value, ["Slot", key], owner, class_name)

    def collect(self, value, path, owner, class_name):
        if isinstance(value, dict):
            if "drawAs" in value and "tintColor" in value:
                self.brush(value, path, owner)
                return
            if "size" in value and ("typefaceFontName" in value or "fontObject" in value):
                self.font(value, path, owner)
                return
            if is_color(value.get("specifiedColor")):
                if value.get("colorUseRule", "UseColor_Specified") == "UseColor_Specified":
                    self.color(value["specifiedColor"], color_category(class_name, path), owner, path)
                return
            if is_color(value):
                self.color(value, color_category(class_name, path), owner, path)
                return
            if {"left", "top", "right", "bottom"} <= set(value) and path[-1].lower().endswith("padding"):
                for side in ("left", "top", "right", "bottom"):
                    if isinstance(value.get(side), (int, float)) and value[side]:
                        self.spacing[value[side]].append(f"{owner}.{'.'.join(path)}")
                return
            for key, child in value.items():
                self.collect(child, path + [key], owner, class_name)
        elif isinstance(value, list):
            for index, child in enumerate(value):
                self.collect(child, path + [str(index)], owner, class_name)

    def color(self, value, category, owner, path):
        if value["a"] > 0:
            self.colors.append({"linear": (value["r"], value["g"], value["b"], value["a"]), "category": category,
                                "where": f"{owner}.{'.'.join(path)}"})

    def brush(self, brush, path, owner):
        draw = brush.get("drawAs")
        if draw in (None, "NoDrawType"):
            return
        where = f"{owner}.{'.'.join(path)}"
        resource = brush.get("resourceObject")
        tint = (brush.get("tintColor") or {}).get("specifiedColor")
        if is_color(tint):
            self.color(tint, "texture tint" if resource else "fill", owner, path)
        if resource:
            self.textures[str(resource)].append(where)
        if draw != "RoundedBox":
            return
        outline = brush.get("outlineSettings") or {}
        width = outline.get("width") or 0
        line = (outline.get("color") or {}).get("specifiedColor")
        if width > 0 and is_color(line) and line["a"] > 0:
            self.color(line, "line", owner, path + ["outline"])
            self.widths[width].append(where)
        if outline.get("roundingType") == "HalfHeightRadius":
            self.radii["pill"].append(where)
        else:
            corners = outline.get("cornerRadii") or {}
            values = sorted({corners[key] for key in "xyzw" if isinstance(corners.get(key), (int, float))})
            self.radii["/".join(number_text(value) for value in values) or "0"].append(where)

    def font(self, font, path, owner):
        family = str(font.get("fontObject") or "").rsplit("/", 1)[-1].split(".")[0] or "(no font)"
        self.fonts[(family, str(font.get("typefaceFontName") or ""), font.get("size"))].append(f"{owner}.{'.'.join(path)}")
        outline = font.get("outlineSettings") or {}
        if (outline.get("outlineSize") or 0) > 0 and is_color(outline.get("outlineColor")):
            self.color(outline["outlineColor"], "text outline", owner, path)


def color_category(class_name, path):
    root = path[1] if path[:1] == ["Slot"] and len(path) > 1 else (path[0] if path else "")
    if root == "ColorAndOpacity":
        return "text" if class_name in TEXT_CLASSES else "tint"
    if root == "ShadowColorAndOpacity":
        return "text shadow"
    if root == "FillColorAndOpacity":
        return "bar fill"
    return (root[:1].lower() + root[1:]) or "color"


def theme_tokens(values, prefix=""):
    """(name, color) pairs of the colors in a theme data asset, without the defaults of brushes that have no image."""
    tokens = []
    for key, value in (values or {}).items():
        name = prefix + key
        if is_color(value):
            tokens.append((name, value))
        elif isinstance(value, dict):
            if "drawAs" in value and "tintColor" in value:
                tint = (value.get("tintColor") or {}).get("specifiedColor")
                if value.get("resourceObject") and is_color(tint):
                    tokens.append((name + ".tint", tint))
            elif is_color(value.get("specifiedColor")):
                tokens.append((name, value["specifiedColor"]))
            else:
                tokens.extend(theme_tokens(value, name + "."))
    return tokens


def summarize(record, project, path, theme_path, theme_values):
    tokens = [{"name": name, "hex": hex_color(tuple(to_srgb(value[key]) for key in "rgb")), "alpha": round(value["a"], 2),
               "linear": [round(value[key], 4) for key in "rgba"]} for name, value in theme_tokens(theme_values)]

    groups = collections.defaultdict(list)
    for entry in record.colors:
        groups[(tuple(to_srgb(channel) for channel in entry["linear"][:3]), round(entry["linear"][3], 2))].append(entry)
    clusters = []
    for (rgb, alpha), members in sorted(groups.items(), key=lambda item: -len(item[1])):
        for cluster in clusters:
            if distance(cluster["rgb"], rgb) <= SAME_COLOR and abs(cluster["alpha"] - alpha) <= SAME_ALPHA:
                cluster["members"].extend(members)
                cluster["variants"].add(f"{hex_color(rgb)} {alpha:.2f}")
                break
        else:
            clusters.append({"rgb": rgb, "alpha": alpha, "linear": members[0]["linear"], "members": list(members),
                             "variants": {f"{hex_color(rgb)} {alpha:.2f}"}})
    clusters.sort(key=lambda cluster: -len(cluster["members"]))

    colors = []
    per_category = collections.Counter()
    for cluster in clusters:
        categories = collections.Counter(member["category"] for member in cluster["members"])
        dominant = categories.most_common(1)[0][0]
        matches = [token["name"] for token in tokens if distance(hex_rgb(token["hex"]), cluster["rgb"]) <= SAME_COLOR]
        per_category[dominant] += 1
        colors.append({
            "name": matches[0] if matches else f"{dominant.replace(' ', '-')}-{per_category[dominant]}",
            "hex": hex_color(cluster["rgb"]),
            "alpha": cluster["alpha"],
            "linear": [round(value, 4) for value in cluster["linear"]],
            "count": len(cluster["members"]),
            "categories": dict(categories.most_common()),
            "theme": matches,
            "variants": sorted(cluster["variants"]),
            "nearDuplicateOf": None,
            "usedIn": sorted({member["where"] for member in cluster["members"]}),
        })
    for index, color in enumerate(colors):
        if color["count"] <= 2:
            for other in colors[:index]:
                if other["count"] > color["count"] and distance(hex_rgb(other["hex"]), hex_rgb(color["hex"])) <= NEAR_COLOR:
                    color["nearDuplicateOf"] = other["hex"]
                    break

    spacing_values = sorted(record.spacing.items())
    spacing_total = sum(len(uses) for _, uses in spacing_values) or 1

    def grid_share(step):
        return round(sum(len(uses) for value, uses in spacing_values if value % step == 0) / spacing_total, 2)

    return {
        "generated": datetime.datetime.now().isoformat(timespec="seconds"),
        "project": project,
        "path": path,
        "theme": theme_path,
        "widgetBlueprints": record.blueprints,
        "widgetCount": record.widget_count,
        "colors": colors,
        "themeColors": tokens,
        "fonts": [{"family": family, "typeface": typeface, "size": size, "count": len(uses), "usedIn": sorted(set(uses))}
                  for (family, typeface, size), uses in sorted(record.fonts.items(), key=lambda item: (-(item[0][2] or 0), item[0][1]))],
        "radii": [{"value": value, "count": len(uses), "usedIn": sorted(set(uses))}
                  for value, uses in sorted(record.radii.items(), key=lambda item: -len(item[1]))],
        "lineWidths": [{"value": value, "count": len(uses), "usedIn": sorted(set(uses))} for value, uses in sorted(record.widths.items())],
        "spacing": {
            "values": [{"value": value, "count": len(uses)} for value, uses in spacing_values],
            "grid4": grid_share(4),
            "grid8": grid_share(8),
            "offGrid4": [{"value": value, "usedIn": sorted(set(uses))} for value, uses in spacing_values if value % 4],
        },
        "sizes": [{"property": key, "value": value, "count": len(uses), "usedIn": sorted(set(uses))}
                  for (key, value), uses in sorted(record.sizes.items())],
        "textures": [{"texture": texture, "usedIn": sorted(set(uses))} for texture, uses in sorted(record.textures.items())],
    }


STYLE = """
body { margin: 0; padding: 24px; background: #10141b; color: #e8edf3; font: 14px/1.5 "Segoe UI", "Malgun Gothic", sans-serif; }
h1 { margin: 0 0 4px; font-size: 22px; } h2 { margin: 28px 0 10px; font-size: 16px; }
.muted { color: #8d9aab; } .warn { color: #ffcf70; } .summary span { display: inline-block; margin: 6px 8px 0 0; padding: 2px 10px;
  border-radius: 999px; background: #1f2a38; }
.wrap { overflow-x: auto; } table { border-collapse: collapse; width: 100%; font-size: 13px; }
th, td { text-align: left; vertical-align: top; padding: 6px 10px 6px 0; border-bottom: 1px solid #222d3b; }
th { color: #8d9aab; font-weight: 600; }
.swatch { display: inline-block; width: 56px; height: 36px; border-radius: 6px; overflow: hidden; background-color: #fff;
  background-image: linear-gradient(45deg, #bbb 25%, transparent 25%), linear-gradient(-45deg, #bbb 25%, transparent 25%),
  linear-gradient(45deg, transparent 75%, #bbb 75%), linear-gradient(-45deg, transparent 75%, #bbb 75%);
  background-size: 12px 12px; background-position: 0 0, 0 6px, 6px -6px, -6px 0; }
.swatch span { display: block; width: 100%; height: 100%; }
ul.uses { margin: 0; padding-left: 16px; color: #aab6c4; font-size: 12px; word-break: break-all; }
.sample { color: #fff; white-space: nowrap; }
"""


def swatch(hex_value, alpha):
    red, green, blue = hex_rgb(hex_value)
    return f'<span class="swatch"><span style="background:rgba({red},{green},{blue},{alpha})"></span></span>'


def use_list(items, limit=6):
    shown = "".join(f"<li>{html.escape(item)}</li>" for item in items[:limit])
    more = f'<li class="muted">and {len(items) - limit} more</li>' if len(items) > limit else ""
    return f'<ul class="uses">{shown}{more}</ul>'


def table(headers, rows):
    head = "".join(f"<th>{html.escape(header)}</th>" for header in headers)
    return f'<div class="wrap"><table><tr>{head}</tr>{"".join(rows)}</table></div>'


def render(summary):
    colors = summary["colors"]
    matched = sum(1 for color in colors if color["theme"])
    near = sum(1 for color in colors if color["nearDuplicateOf"])
    color_rows = []
    for color in colors:
        theme = html.escape(", ".join(color["theme"])) if color["theme"] else '<span class="warn">not in the theme</span>'
        duplicate = f'<div class="warn">near duplicate of {color["nearDuplicateOf"]}</div>' if color["nearDuplicateOf"] else ""
        variants = f'<div class="muted">also {html.escape(", ".join(color["variants"][1:]))}</div>' if len(color["variants"]) > 1 else ""
        categories = ", ".join(f"{name} {count}" for name, count in color["categories"].items())
        linear = ", ".join(f"{value:.3f}" for value in color["linear"])
        color_rows.append(f"<tr><td>{swatch(color['hex'], color['alpha'])}</td><td><b>{html.escape(color['name'])}</b><br>"
                          f"{color['hex']} alpha {color['alpha']:.2f}<div class='muted'>linear {linear}</div>{variants}{duplicate}</td>"
                          f"<td>{color['count']}</td><td>{html.escape(categories)}</td><td>{theme}</td><td>{use_list(color['usedIn'])}</td></tr>")
    theme_rows = [f"<tr><td>{swatch(token['hex'], token['alpha'])}</td><td><b>{html.escape(token['name'])}</b></td><td>{token['hex']} "
                  f"alpha {token['alpha']:.2f}</td><td class='muted'>linear {', '.join(f'{v:.3f}' for v in token['linear'])}</td></tr>"
                  for token in summary["themeColors"]]
    font_rows = []
    for font in summary["fonts"]:
        weight = 700 if "bold" in font["typeface"].lower() else 400
        pixels = (font["size"] or 0) * 4 / 3
        font_rows.append(f"<tr><td>{html.escape(font['family'])} {html.escape(font['typeface'])}</td><td>{number_text(font['size'] or 0)}</td>"
                         f"<td>{font['count']}</td><td><span class='sample' style='font: {weight} {pixels:.0f}px/1.1 Roboto, \"Segoe UI\", sans-serif'>"
                         f"Aa 가나 123</span></td><td>{use_list(font['usedIn'])}</td></tr>")
    shape_rows = ([f"<tr><td>corner radius</td><td>{html.escape(str(radius['value']))}</td><td>{radius['count']}</td><td>{use_list(radius['usedIn'])}</td></tr>"
                   for radius in summary["radii"]]
                  + [f"<tr><td>line width</td><td>{number_text(width['value'])}</td><td>{width['count']}</td><td>{use_list(width['usedIn'])}</td></tr>"
                     for width in summary["lineWidths"]])
    spacing = summary["spacing"]
    spacing_values = ", ".join(f"{number_text(item['value'])} ({item['count']})" for item in spacing["values"]) or "none"
    off_grid = "".join(f"<tr><td>{number_text(item['value'])}</td><td>{use_list(item['usedIn'])}</td></tr>" for item in spacing["offGrid4"])
    size_rows = [f"<tr><td>{html.escape(size['property'])}</td><td>{number_text(size['value'])}</td><td>{size['count']}</td>"
                 f"<td>{use_list(size['usedIn'])}</td></tr>" for size in summary["sizes"]]
    texture_rows = [f"<tr><td>{html.escape(texture['texture'])}</td><td>{use_list(texture['usedIn'])}</td></tr>" for texture in summary["textures"]]

    return (f"<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
            f"<title>UI style of {html.escape(summary['path'])}</title><style>{STYLE}</style></head><body>"
            f"<h1>UI style used under {html.escape(summary['path'])}</h1>"
            f"<div class='muted'>{html.escape(str(summary['project']))} - {len(summary['widgetBlueprints'])} Widget Blueprints, "
            f"{summary['widgetCount']} widgets - theme {html.escape(summary['theme'] or 'not given')} - {summary['generated']}</div>"
            f"<div class='summary'><span>{len(colors)} colors</span><span>{matched} in the theme</span><span>{len(colors) - matched} not in the theme</span>"
            f"<span>{near} near duplicates</span><span>{len(summary['fonts'])} text styles</span>"
            f"<span>{int(spacing['grid4'] * 100)}% of spacing on a 4 grid, {int(spacing['grid8'] * 100)}% on 8</span></div>"
            f"<p class='muted'>Values that code or Blueprint graphs set at runtime, such as colors read from the theme, are not in the Widget "
            f"Blueprints; they show here only as the placeholders the widgets were built with.</p>"
            f"<h2>Colors</h2>{table(['', 'Color', 'Uses', 'As', 'Theme token', 'Used in'], color_rows)}"
            f"<h2>Theme tokens</h2>{table(['', 'Token', 'sRGB', 'Linear'], theme_rows) if theme_rows else '<p class=muted>No theme was read.</p>'}"
            f"<h2>Text styles</h2>{table(['Font', 'Size', 'Uses', 'Sample', 'Used in'], font_rows)}"
            f"<h2>Shapes</h2>{table(['', 'Value', 'Uses', 'Used in'], shape_rows)}"
            f"<h2>Spacing</h2><p>{spacing_values}</p>"
            f"{table(['Not on a 4 grid', 'Used in'], [off_grid]) if off_grid else ''}"
            f"<h2>Fixed sizes</h2>{table(['Property', 'Value', 'Uses', 'Used in'], size_rows)}"
            f"<h2>Textures</h2>{table(['Texture', 'Used in'], texture_rows) if texture_rows else '<p class=muted>None.</p>'}"
            f"</body></html>\n")


def main():
    parser = argparse.ArgumentParser(description="Extracts the style that Widget Blueprints use.")
    parser.add_argument("--url", default=os.environ.get("AGENT_MCP_URL", "http://127.0.0.1:18765/mcp"))
    parser.add_argument("--token", default=os.environ.get("AGENT_MCP_TOKEN"))
    parser.add_argument("--path", default="/Game", help="folder of the Widget Blueprints, for example /Game/UI")
    parser.add_argument("--theme", help="object path of the theme data asset, for example /Game/UI/DA_Theme.DA_Theme")
    parser.add_argument("--name", help="file name of the report without extension; default: the folder path")
    parser.add_argument("--out", default=os.path.join("Saved", "UiStyle"), help="folder of the report")
    args = parser.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    if not args.path.startswith("/"):
        hint = " (Git Bash turns arguments that start with / into Windows paths; set MSYS_NO_PATHCONV=1)" if ":" in args.path else ""
        print(f"--path must be a content folder such as /Game/UI, not {args.path}{hint}", file=sys.stderr)
        return 2

    record = Record()
    client = McpClient(args.url, args.token)
    try:
        client.start()
        project = client.call("editor_get_state", {"maxListedItems": 0}).get("project")
        cursor = 0
        paths = []
        while cursor is not None and cursor >= 0:
            found = client.call("asset_find", {"path": args.path, "assetClass": "WidgetBlueprint", "limit": 500, "cursor": cursor})
            paths += [asset["path"] for asset in found.get("assets") or []]
            cursor = found.get("nextCursor", -1)
        for path in paths:
            record.add_tree(path, client.call("umg_inspect", {"widgetBlueprint": path, "bIncludeProperties": True, "bIncludeSlots": True,
                                                              "maxWidgets": 2000}))
        theme_values = client.call("object_get_properties", {"object": args.theme}).get("values") if args.theme else None
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Cannot read the Widget Blueprints from {args.url}: {error}", file=sys.stderr)
        return 1
    finally:
        client.close()

    summary = summarize(record, project, args.path, args.theme, theme_values)
    name = args.name or (args.path.strip("/").replace("/", "_") or "Game")
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, name + ".json"), "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2, ensure_ascii=False)
    with open(os.path.join(out, name + ".html"), "w", encoding="utf-8") as handle:
        handle.write(render(summary))

    colors = summary["colors"]
    matched = sum(1 for color in colors if color["theme"])
    print(f"{project}: {len(paths)} Widget Blueprints, {record.widget_count} widgets under {args.path}")
    print(f"Colors: {len(colors)} from {len(record.colors)} uses; {matched} match theme tokens, {len(colors) - matched} do not, "
          f"{sum(1 for color in colors if color['nearDuplicateOf'])} near duplicates")
    print("Text styles: " + ", ".join(f"{font['typeface']} {number_text(font['size'] or 0)} x{font['count']}" for font in summary["fonts"]))
    print("Corner radii: " + ", ".join(f"{radius['value']} x{radius['count']}" for radius in summary["radii"])
          + " | line widths: " + ", ".join(f"{number_text(width['value'])} x{width['count']}" for width in summary["lineWidths"]))
    print(f"Spacing: {len(summary['spacing']['values'])} values, {int(summary['spacing']['grid4'] * 100)}% on a 4 grid, "
          f"{int(summary['spacing']['grid8'] * 100)}% on an 8 grid")
    print(f"Report: {os.path.join(out, name + '.html')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
