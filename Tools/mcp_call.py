#!/usr/bin/env python3
"""Calls one Agent MCP tool and prints the result (standard library only).

Usage:
  python mcp_call.py <tool_name> [json_arguments] [--url URL] [--expect-project NAME] [--token TOKEN]
                     [--save-images DIR] [--timeout SECONDS]

Examples:
  python mcp_call.py editor_get_state
  python mcp_call.py actor_find '{"name": "Floor"}' --expect-project MyGame
  python mcp_call.py viewport_capture '{"maxWidth": 800}' --save-images Saved/MCP

Every editor with the plugin enabled serves the same default port, so check which project answers before calling tools
that change anything: the project is printed on stderr, and --expect-project refuses to call the tool for another project.

The text content is printed to stdout, a summary line to stderr.
Exit code: 0 on success, 1 when the tool returned isError or the request failed, 2 for invalid arguments,
3 when the server belongs to a different project than --expect-project.
"""

import argparse
import base64
import json
import os
import sys
import time

from mcp_smoke import McpClient


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("tool")
    parser.add_argument("arguments", nargs="?", default="{}", help="tool arguments as a JSON object")
    parser.add_argument("--url", default="http://127.0.0.1:18765/mcp")
    parser.add_argument("--expect-project", default=None, help="refuse to call the tool unless the editor belongs to this project")
    parser.add_argument("--token", default=None)
    parser.add_argument("--save-images", default=None, help="directory to write image content to")
    parser.add_argument("--max-chars", type=int, default=20000, help="truncate printed text after this many characters")
    parser.add_argument("--timeout", type=float, default=600.0)
    args = parser.parse_args()

    try:
        arguments = json.loads(args.arguments)
    except json.JSONDecodeError as error:
        print(f"arguments are not valid JSON: {error}", file=sys.stderr)
        return 2
    if not isinstance(arguments, dict):
        print("arguments must be a JSON object", file=sys.stderr)
        return 2

    client = McpClient(args.url, args.token, timeout=args.timeout)
    status, headers, payload, _ = client.request("initialize", {
        "protocolVersion": "2025-06-18",
        "capabilities": {},
        "clientInfo": {"name": "mcp_call.py", "version": "1.1"},
    }, use_session=False)
    if status != 200 or not headers or not headers.get("Mcp-Session-Id"):
        print(f"initialize failed: HTTP {status} {payload}", file=sys.stderr)
        return 1
    client.session_id = headers.get("Mcp-Session-Id")
    client.send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    _, _, state_error, state, _ = client.call_tool("editor_get_state", {"maxListedItems": 0})
    project = None if state_error else (state or {}).get("project")
    print(f"-- connected to project '{project}' at {args.url}", file=sys.stderr)
    if args.expect_project and project != args.expect_project:
        print(f"refusing to call {args.tool}: the editor at {args.url} belongs to project '{project}', expected '{args.expect_project}'",
              file=sys.stderr)
        client.send(method="DELETE")
        return 3

    started = time.time()
    status, payload, is_error, _, size = client.call_tool(args.tool, arguments)
    elapsed = time.time() - started
    client.send(method="DELETE")

    rpc_error = (payload or {}).get("error")
    if rpc_error:
        print(json.dumps(rpc_error, ensure_ascii=False))
        print(f"-- {args.tool}: HTTP {status}, JSON-RPC error, {elapsed:.2f}s", file=sys.stderr)
        return 1

    for index, item in enumerate(((payload or {}).get("result") or {}).get("content") or []):
        if item.get("type") == "text":
            text = item.get("text", "")
            print(text if len(text) <= args.max_chars else f"{text[:args.max_chars]}... ({len(text)} characters)")
        elif item.get("type") == "image":
            data = base64.b64decode(item.get("data", ""))
            line = f"[image {item.get('mimeType')}, {len(data)} bytes]"
            if args.save_images:
                os.makedirs(args.save_images, exist_ok=True)
                extension = ".png" if item.get("mimeType") == "image/png" else ".bin"
                path = os.path.join(args.save_images, f"{args.tool}_{time.strftime('%Y%m%d_%H%M%S')}_{index}{extension}")
                with open(path, "wb") as handle:
                    handle.write(data)
                line += f" saved to {os.path.abspath(path)}"
            print(line)

    print(f"-- {args.tool}: HTTP {status}, isError={is_error}, {size} bytes, {elapsed:.2f}s", file=sys.stderr)
    return 1 if is_error else 0


if __name__ == "__main__":
    sys.exit(main())
