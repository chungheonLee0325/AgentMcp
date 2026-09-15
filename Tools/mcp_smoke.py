#!/usr/bin/env python3
"""Agent MCP smoke test (standard library only).

Checks the MCP transport contract and the tools against a running editor:
  P0       initialize, notifications, tools/list, editor_get_state, log_get_recent, tool and protocol errors
  Slice 1  actor_find -> actor_inspect -> object properties -> write -> undo/redo -> transform -> PIE -> log
  P1       asset registry, class hierarchy, blueprint_inspect, umg_inspect, datatable_* on engine content
  P2       DataTable edits, dry run, undo and asset_save on the testbed fixtures
  P3       blueprint_compile, BindWidget checks, PIE refusal, viewport_capture with the game UI, rollback, cancellation, livecoding_compile
  P4       umg_create_widget_blueprint, umg_add_widgets, umg_set_widget_properties, umg_remove_widgets: checks, undo, redo, PIE refusal
  P5       skills_list, skills_get: the plugin skill, project skill folders, replacing a skill, skill files, refusals
  P6       asset_create, asset_import_textures, object_set_properties on assets: checks, undo, replacing, saving, PIE refusal

Level writes are undone again. P2, P3, P4 and P6 need the AgentMcpTestbed "testbed" toolset, which resets and saves its fixture assets.
P5 writes temporary skills to Saved/MCP/SmokeSkills, which the testbed configuration adds to SkillDirectories. P6 writes images to
Saved/MCP/SmokeImport of the project.

Usage:
  python mcp_smoke.py [--url http://127.0.0.1:18766/mcp] [--expect-project AgentMcpTestbed] [--token TOKEN] [--out evidence.json]
                      [--skip-slice1] [--skip-pie] [--pie-cycles 3] [--skip-p1] [--skip-p2] [--skip-p3] [--skip-p4] [--skip-p5]
                      [--skip-p6]

Before any check, the script asks the editor at --url for its project and stops unless it is --expect-project: the checks
start and stop Play In Editor, change the level and save assets.
"""

import argparse
import base64
import datetime
import json
import os
import shutil
import struct
import sys
import threading
import time
import urllib.error
import urllib.request
import zlib

P0_TOOLS = {"editor_get_state", "log_get_recent", "editor_undo", "editor_redo"}
SLICE1_TOOLS = {
    "actor_find", "actor_inspect", "actor_set_transform",
    "object_list_properties", "object_get_properties", "object_set_properties",
    "pie_start", "pie_stop", "pie_status",
}
P1_TOOLS = {
    "asset_find", "asset_inspect", "asset_referencers", "asset_dependencies", "class_find_derived",
    "blueprint_inspect", "umg_inspect", "datatable_get_schema", "datatable_list_rows", "datatable_get_rows",
}
SMOKE_TAG = "AgentMcpSmoke"


class McpClient:
    def __init__(self, url, token=None, timeout=300.0):
        self.url = url
        self.token = token
        self.timeout = timeout
        self.session_id = None
        self.next_id = 1

    def send(self, message=None, raw_body=None, use_session=True, extra_headers=None, method="POST"):
        body = raw_body if raw_body is not None else (json.dumps(message).encode("utf-8") if message is not None else None)
        request = urllib.request.Request(self.url, data=body, method=method)
        request.add_header("Content-Type", "application/json")
        request.add_header("Accept", "application/json, text/event-stream")
        if self.token:
            request.add_header("Authorization", "Bearer " + self.token)
        if use_session and self.session_id:
            request.add_header("Mcp-Session-Id", self.session_id)
        for name, value in (extra_headers or {}).items():
            request.add_header(name, value)
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                status, headers, text = response.status, response.headers, response.read().decode("utf-8")
        except urllib.error.HTTPError as error:
            status, headers, text = error.code, error.headers, error.read().decode("utf-8")
        payload = None
        if text.strip():
            try:
                payload = json.loads(text)
            except json.JSONDecodeError:
                payload = {"_raw": text}
        return status, headers, payload, len(text.encode("utf-8"))

    def request(self, method, params=None, **kwargs):
        message = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        self.next_id += 1
        if params is not None:
            message["params"] = params
        return self.send(message, **kwargs)

    def call_tool(self, name, arguments=None):
        status, _, payload, size = self.request("tools/call", {"name": name, "arguments": arguments or {}})
        result = (payload or {}).get("result") or {}
        text = "".join(item.get("text", "") for item in result.get("content", []) if item.get("type") == "text")
        try:
            data = json.loads(text) if text else None
        except json.JSONDecodeError:
            data = None
        return status, payload, result.get("isError", False), data, size


class Report:
    def __init__(self):
        self.checks = []

    def check(self, name, passed, detail=""):
        self.checks.append({"name": name, "passed": bool(passed), "detail": detail})
        print(("PASS " if passed else "FAIL ") + name + (" - " + detail if detail else ""), flush=True)
        return bool(passed)

    def info(self, name, detail):
        self.checks.append({"name": name, "passed": None, "detail": detail})
        print("INFO " + name + " - " + detail, flush=True)


def error_code(data):
    return ((data or {}).get("error") or {}).get("code")


def error_message(data):
    return ((data or {}).get("error") or {}).get("message") or ""


def close_enough(actual, expected, tolerance=0.01):
    return (isinstance(actual, list) and isinstance(expected, list) and len(actual) == len(expected)
            and all(abs(a - e) <= tolerance for a, e in zip(actual, expected)))


def read_property(client, path, name):
    _, _, is_error, data, _ = client.call_tool("object_get_properties", {"object": path, "propertyNames": [name]})
    return None if is_error else ((data or {}).get("values") or {}).get(name)


def get_undo_state(client):
    _, _, is_error, data, _ = client.call_tool("editor_get_state", {"maxListedItems": 0})
    return {} if is_error else ((data or {}).get("undo") or {})


def run_p0(client, report, evidence, expected_tools):
    status, headers, payload, _ = client.request("initialize", {
        "protocolVersion": "2025-06-18",
        "capabilities": {},
        "clientInfo": {"name": "mcp_smoke.py", "version": "1.2"},
    }, use_session=False)
    client.session_id = headers.get("Mcp-Session-Id") if headers else None
    result = (payload or {}).get("result") or {}
    report.check("initialize returns 200 with a session id", status == 200 and bool(client.session_id), f"status={status}")
    report.check("initialize negotiates protocol 2025-06-18", result.get("protocolVersion") == "2025-06-18", str(result.get("protocolVersion")))
    evidence["initialize"] = result

    status, _, _, _ = client.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
    report.check("notifications/initialized returns 202", status == 202, f"status={status}")

    status, _, payload, _ = client.request("ping")
    report.check("ping returns an empty result", status == 200 and (payload or {}).get("result") == {}, f"status={status}")

    status, _, payload, list_bytes = client.request("tools/list")
    tools = ((payload or {}).get("result") or {}).get("tools") or []
    tool_names = sorted(tool.get("name") for tool in tools)
    report.check("tools/list returns tools", status == 200 and len(tools) > 0, f"{len(tools)} tools")
    missing = sorted(expected_tools - set(tool_names))
    report.check("tools/list includes the expected tools", not missing, "missing: " + ", ".join(missing) if missing else ", ".join(tool_names))
    report.info("tools/list size", f"{list_bytes} bytes")
    evidence["toolsList"] = {"bytes": list_bytes, "names": tool_names, "tools": tools}

    status, _, is_error, data, size = client.call_tool("editor_get_state")
    report.check("editor_get_state succeeds", status == 200 and not is_error and bool((data or {}).get("project")), f"{size} bytes")
    evidence["editor_get_state"] = data

    status, _, is_error, data, _ = client.call_tool("log_get_recent", {"maxEntries": 5})
    report.check("log_get_recent returns entries and nextSequence", status == 200 and not is_error and "nextSequence" in (data or {}), f"{len((data or {}).get('entries', []))} entries")
    next_sequence = (data or {}).get("nextSequence", 0)

    status, _, is_error, data, _ = client.call_tool("log_get_recent", {"sinceSequence": next_sequence, "maxEntries": 50})
    report.check("log_get_recent incremental poll succeeds", status == 200 and not is_error, f"{len((data or {}).get('entries', []))} new entries")

    status, _, is_error, data, _ = client.call_tool("log_get_recent", {"minVerbosity": "Loud"})
    report.check("invalid enum value returns INVALID_ARGUMENT", is_error and error_code(data) == "INVALID_ARGUMENT", error_message(data)[:160])

    status, _, is_error, data, _ = client.call_tool("editor_get_state", {"maxListedItems": "many"})
    report.check("wrongly typed argument returns INVALID_ARGUMENT", is_error and error_code(data) == "INVALID_ARGUMENT", str(error_code(data)))

    status, _, is_error, data, _ = client.call_tool("editor_get_state", {"noSuchArgument": 1})
    report.check("unknown argument returns INVALID_ARGUMENT", is_error and error_code(data) == "INVALID_ARGUMENT", str((data or {}).get("error")))

    if not get_undo_state(client).get("bCanUndo"):
        status, _, is_error, data, _ = client.call_tool("editor_undo")
        report.check("editor_undo with an empty history returns NOTHING_TO_UNDO", is_error and error_code(data) == "NOTHING_TO_UNDO", str(error_code(data)))


def run_slice1(client, report, evidence, run_pie, pie_cycles):
    slice1 = evidence.setdefault("slice1", {})

    # --- actor_find -------------------------------------------------------------------------------
    _, _, is_error, data, size = client.call_tool("actor_find", {"limit": 500})
    actors = (data or {}).get("actors") or []
    report.check("actor_find lists editor level actors", not is_error and len(actors) > 0 and (data or {}).get("world") == "Editor",
                 f"{(data or {}).get('totalMatched')} actors in {(data or {}).get('worldPackage')}, {size} bytes, error={error_code(data)}")
    slice1["actor_find"] = data

    _, _, is_error, data, _ = client.call_tool("actor_find", {"actorClass": "StaticMeshActor", "limit": 10})
    meshes = (data or {}).get("actors") or []
    report.check("actor_find filters by class name", not is_error and len(meshes) > 0, ", ".join(actor.get("label", "") for actor in meshes) or str(error_code(data)))

    _, _, is_error, data, _ = client.call_tool("actor_find", {"actorClass": "NoSuchActorClass_AgentMcp"})
    report.check("actor_find with an unknown class returns NOT_FOUND", is_error and error_code(data) == "NOT_FOUND", str(error_code(data)))

    if len(actors) > 1:
        _, _, first_error, first_page, _ = client.call_tool("actor_find", {"limit": 1})
        next_cursor = (first_page or {}).get("nextCursor")
        _, _, second_error, second_page, _ = client.call_tool("actor_find", {"limit": 1, "cursor": next_cursor if isinstance(next_cursor, int) else 1})
        first = (((first_page or {}).get("actors") or [{}])[0]).get("path")
        second = (((second_page or {}).get("actors") or [{}])[0]).get("path")
        report.check("actor_find pages with nextCursor", not first_error and not second_error and next_cursor == 1
                     and first == actors[0]["path"] and second == actors[1]["path"], f"{first} -> {second}")

    _, _, is_error, data, _ = client.call_tool("actor_find", {"world": "Play"})
    report.check("actor_find in the play world without a session returns PIE_NOT_ACTIVE", is_error and error_code(data) == "PIE_NOT_ACTIVE", str(error_code(data)))

    target = (meshes or actors or [None])[0]
    if not report.check("a target actor exists for the inspection and write checks", target is not None, str(target)):
        return
    path, label = target["path"], target["label"]
    slice1["target"] = target

    # --- actor_inspect ----------------------------------------------------------------------------
    _, _, is_error, details, size = client.call_tool("actor_inspect", {"actor": path})
    transform = (details or {}).get("transform") or {}
    components = (details or {}).get("components") or []
    report.check("actor_inspect returns transform and components", not is_error and len(transform.get("location") or []) == 3 and (details or {}).get("componentCount", 0) > 0,
                 f"{size} bytes, {len(components)} components, error={error_code(details)}")
    slice1["actor_inspect"] = details
    root = next((component for component in components if component.get("bIsRoot")), None)

    _, _, is_error, data, _ = client.call_tool("actor_inspect", {"actor": label, "bIncludeComponents": False})
    report.check("actor_inspect accepts an actor label", not is_error and (data or {}).get("path") == path, f"{label} -> {(data or {}).get('path')}")

    _, _, is_error, data, _ = client.call_tool("actor_inspect", {"actor": "NoSuchActor_AgentMcp"})
    report.check("actor_inspect with an unknown actor returns NOT_FOUND", is_error and error_code(data) == "NOT_FOUND", str(error_code(data)))

    # --- object properties ------------------------------------------------------------------------
    _, _, is_error, data, _ = client.call_tool("object_list_properties", {"object": path, "filter": "Tags"})
    tags_info = next((item for item in (data or {}).get("properties") or [] if item.get("name") == "Tags"), None)
    report.check("object_list_properties marks Tags as editable", not is_error and bool(tags_info) and tags_info.get("bEditable") is True, json.dumps(tags_info))

    read_only_name = "StaticMeshComponent" if target.get("className") == "StaticMeshActor" else "RootComponent"
    _, _, is_error, data, _ = client.call_tool("object_list_properties", {"object": path, "filter": read_only_name})
    read_only_info = next((item for item in (data or {}).get("properties") or [] if item.get("name") == read_only_name), None)
    report.check(f"object_list_properties marks {read_only_name} as not editable with a reason",
                 not is_error and bool(read_only_info) and read_only_info.get("bEditable") is False and bool(read_only_info.get("notEditableReason")), json.dumps(read_only_info))

    _, _, is_error, data, _ = client.call_tool("object_get_properties", {"object": path, "propertyNames": ["Tags", "bHidden", "NoSuchProperty_AgentMcp"]})
    values = (data or {}).get("values") or {}
    report.check("object_get_properties returns values and lists missing names",
                 not is_error and "Tags" in values and "bHidden" in values and "NoSuchProperty_AgentMcp" in ((data or {}).get("missing") or []), json.dumps(data)[:200])
    original_tags = values.get("Tags") or []
    original_hidden = values.get("bHidden")

    if root:
        component_reference = f"{label}.{root['name']}"
        _, _, is_error, data, _ = client.call_tool("object_get_properties", {"object": component_reference, "propertyNames": ["RelativeLocation"]})
        report.check("object_get_properties accepts ActorLabel.ComponentName", not is_error and "RelativeLocation" in ((data or {}).get("values") or {}),
                     f"{component_reference}: {json.dumps((data or {}).get('values'))}")

    # --- write -> undo -> redo --------------------------------------------------------------------
    undoable_before = get_undo_state(client).get("undoableCount")
    new_tags = original_tags + [SMOKE_TAG]
    _, _, is_error, data, _ = client.call_tool("object_set_properties", {"object": path, "values": {"Tags": new_tags}})
    slice1["object_set_properties"] = data
    report.check("object_set_properties writes Tags and reads them back",
                 not is_error and ((data or {}).get("after") or {}).get("Tags") == new_tags and "Tags" in ((data or {}).get("changed") or []), json.dumps(data)[:240])
    report.check("object_set_properties is recorded as one undoable transaction",
                 ((data or {}).get("undo") or {}).get("recorded") is True and get_undo_state(client).get("undoableCount") == (undoable_before or 0) + 1,
                 json.dumps((data or {}).get("undo")))

    _, _, is_error, data, _ = client.call_tool("editor_undo")
    report.check("editor_undo reverts the property write", not is_error and "object_set_properties" in ((data or {}).get("transaction") or ""), str((data or {}).get("transaction")))
    report.check("Tags are restored after undo", read_property(client, path, "Tags") == original_tags, "")

    _, _, is_error, data, _ = client.call_tool("editor_redo")
    report.check("editor_redo reapplies the property write", not is_error and read_property(client, path, "Tags") == new_tags, str((data or {}).get("transaction")))
    client.call_tool("editor_undo")
    report.check("Tags are restored after undoing the redo", read_property(client, path, "Tags") == original_tags, "")

    if isinstance(original_hidden, bool):
        _, _, is_error, data, _ = client.call_tool("object_set_properties", {"object": path, "values": {"bHidden": not original_hidden}})
        report.check("object_set_properties writes a bitfield bool", not is_error and ((data or {}).get("after") or {}).get("bHidden") == (not original_hidden), json.dumps((data or {}).get("after")))
        client.call_tool("editor_undo")
        report.check("the bitfield bool is restored after undo", read_property(client, path, "bHidden") == original_hidden, "")

    undoable_before = get_undo_state(client).get("undoableCount")
    _, _, is_error, data, _ = client.call_tool("object_set_properties", {"object": path, "values": {"Tags": "not-an-array", "NoSuchProperty_AgentMcp": 1}})
    message = error_message(data)
    report.check("invalid values are reported together and nothing changes",
                 is_error and error_code(data) == "INVALID_ARGUMENT" and "NoSuchProperty_AgentMcp" in message and "Tags" in message
                 and read_property(client, path, "Tags") == original_tags and get_undo_state(client).get("undoableCount") == undoable_before, message[:240])

    _, _, is_error, data, _ = client.call_tool("object_set_properties", {"object": path, "values": {read_only_name: None}})
    report.check("a read-only property returns PROPERTY_NOT_EDITABLE", is_error and error_code(data) == "PROPERTY_NOT_EDITABLE", error_message(data)[:200])

    # --- transform --------------------------------------------------------------------------------
    location = transform.get("location") or [0.0, 0.0, 0.0]
    moved = [location[0], location[1], location[2] + 25.0]
    _, _, is_error, data, _ = client.call_tool("actor_set_transform", {"actor": path, "location": moved})
    slice1["actor_set_transform"] = data
    after = (data or {}).get("after") or {}
    report.check("actor_set_transform moves the actor and keeps rotation and scale",
                 not is_error and close_enough(after.get("location"), moved) and close_enough(after.get("rotation"), transform.get("rotation") or [])
                 and close_enough(after.get("scale"), transform.get("scale") or []), json.dumps(after))
    client.call_tool("editor_undo")
    _, _, _, data, _ = client.call_tool("actor_inspect", {"actor": path, "bIncludeComponents": False})
    report.check("the actor transform is restored after undo", close_enough(((data or {}).get("transform") or {}).get("location"), location), json.dumps((data or {}).get("transform")))

    _, _, is_error, data, _ = client.call_tool("actor_set_transform", {"actor": path, "location": [1.0, 2.0]})
    report.check("actor_set_transform rejects a vector without 3 numbers", is_error and error_code(data) == "INVALID_ARGUMENT", error_message(data)[:160])

    _, _, is_error, data, _ = client.call_tool("actor_set_transform", {"actor": path})
    report.check("actor_set_transform without values returns INVALID_ARGUMENT", is_error and error_code(data) == "INVALID_ARGUMENT", error_message(data)[:160])

    # --- PIE --------------------------------------------------------------------------------------
    if not run_pie:
        return

    runs = slice1.setdefault("pie", [])
    _, _, is_error, data, _ = client.call_tool("pie_stop")
    report.check("pie_stop without a session succeeds with bWasActive false", not is_error and (data or {}).get("bWasActive") is False, json.dumps(data)[:160])

    for cycle in range(1, pie_cycles + 1):
        record = {"cycle": cycle}
        runs.append(record)

        started = time.time()
        _, _, is_error, data, _ = client.call_tool("pie_start", {"warmupSeconds": 1.0})
        record["start"] = data
        state = (data or {}).get("playSession") or {}
        if not report.check(f"pie_start begins play (cycle {cycle})", not is_error and state.get("bBegunPlay") is True,
                            f"{time.time() - started:.1f}s wall, startupSeconds={(data or {}).get('startupSeconds')}, world={state.get('world')}, error={error_code(data)} {error_message(data)[:120]}"):
            break
        start_sequence = (data or {}).get("startLogSequence", 0)

        _, _, is_error, data, _ = client.call_tool("pie_status")
        report.check(f"pie_status reports the running session (cycle {cycle})", not is_error and ((data or {}).get("playSession") or {}).get("bBegunPlay") is True, json.dumps((data or {}).get("playSession")))

        _, _, is_error, data, _ = client.call_tool("pie_start")
        report.check(f"a second pie_start returns PIE_ALREADY_ACTIVE (cycle {cycle})", is_error and error_code(data) == "PIE_ALREADY_ACTIVE", str(error_code(data)))

        _, _, is_error, data, _ = client.call_tool("actor_find", {"world": "Play", "limit": 5})
        report.check(f"actor_find searches the play world (cycle {cycle})", not is_error and (data or {}).get("world") == "Play" and (data or {}).get("totalMatched", 0) > 0,
                     f"{(data or {}).get('totalMatched')} actors in {(data or {}).get('worldPackage')}")

        _, _, is_error, data, _ = client.call_tool("object_set_properties", {"object": path, "values": {"Tags": new_tags}})
        report.check(f"writes are blocked during PIE (cycle {cycle})", is_error and error_code(data) == "PIE_ACTIVE", str(error_code(data)))

        _, _, is_error, data, _ = client.call_tool("log_get_recent", {"sinceSequence": start_sequence, "contains": "PIE", "maxEntries": 20})
        entries = (data or {}).get("entries") or []
        report.check(f"log_get_recent returns the session log (cycle {cycle})", not is_error and len(entries) > 0,
                     (entries[0].get("category", "") + ": " + entries[0].get("message", "")[:120]) if entries else "no lines containing PIE")

        started = time.time()
        _, _, is_error, data, _ = client.call_tool("pie_stop")
        record["stop"] = data
        report.check(f"pie_stop ends the session (cycle {cycle})",
                     not is_error and (data or {}).get("bWasActive") is True and ((data or {}).get("playSession") or {}).get("bActive") is False,
                     f"{time.time() - started:.1f}s wall, shutdownSeconds={(data or {}).get('shutdownSeconds')}, error={error_code(data)}")

        _, _, _, data, _ = client.call_tool("log_get_recent", {"sinceSequence": start_sequence, "minVerbosity": "Warning", "maxEntries": 100})
        problems = (data or {}).get("entries") or []
        record["warnings"] = problems
        errors = [entry for entry in problems if entry.get("verbosity") in ("Error", "Fatal")]
        first = (problems[0].get("category", "") + ": " + problems[0].get("message", "")[:160]) if problems else ""
        report.info(f"warnings during PIE cycle {cycle}", f"{len(problems)} warning or error lines" + (f"; first: {first}" if first else ""))
        report.check(f"no error lines during PIE cycle {cycle}", not errors, "; ".join(entry.get("category", "") + ": " + entry.get("message", "")[:120] for entry in errors[:3]))

    _, _, is_error, data, _ = client.call_tool("pie_status")
    report.check("no play session remains after the PIE checks", not is_error and ((data or {}).get("playSession") or {}).get("bActive") is False, json.dumps((data or {}).get("playSession")))


def run_p1(client, report, evidence):
    p1 = evidence.setdefault("p1", {})

    # --- asset registry ---------------------------------------------------------------------------
    _, _, is_error, data, size = client.call_tool("asset_find", {"path": "/Engine/EngineSky", "assetClass": "Blueprint"})
    assets = (data or {}).get("assets") or []
    sky = next((asset for asset in assets if asset.get("name") == "BP_Sky_Sphere"), None)
    report.check("asset_find finds a Blueprint by folder and class", not is_error and sky is not None,
                 f"{len(assets)} assets, {size} bytes, parentClass={(sky or {}).get('parentClass')}, error={error_code(data)} {error_message(data)[:120]}")
    p1["asset_find_blueprint"] = data

    _, _, is_error, data, _ = client.call_tool("asset_find", {"path": "/Engine/Sequencer", "assetClass": "WidgetBlueprint"})
    widget_assets = (data or {}).get("assets") or []
    widget_blueprint = widget_assets[0] if widget_assets else None
    report.check("asset_find finds a Widget Blueprint", not is_error and widget_blueprint is not None,
                 json.dumps(widget_blueprint) if widget_blueprint else f"{error_code(data)} {error_message(data)[:120]}")

    _, _, is_error, data, _ = client.call_tool("asset_find", {"path": "/", "assetClass": "DataTable", "limit": 20})
    tables = (data or {}).get("assets") or []
    report.info("asset_find DataTables in mounted content",
                f"{(data or {}).get('totalMatched')} tables, error={error_code(data)}" + (f", first: {tables[0]['path']}" if tables else ""))

    _, _, is_error, data, _ = client.call_tool("asset_find", {"path": "/"})
    report.check("asset_find over all content without a filter returns INVALID_ARGUMENT", is_error and error_code(data) == "INVALID_ARGUMENT", error_message(data)[:120])

    _, _, is_error, data, _ = client.call_tool("asset_find", {"assetClass": "NoSuchAssetClass_AgentMcp"})
    report.check("asset_find with an unknown class returns NOT_FOUND", is_error and error_code(data) == "NOT_FOUND", str(error_code(data)))

    _, _, is_error, data, _ = client.call_tool("asset_inspect", {"asset": "/Game/NoSuchAsset_AgentMcp"})
    report.check("asset_inspect with an unknown asset returns NOT_FOUND", is_error and error_code(data) == "NOT_FOUND", str(error_code(data)))

    if sky:
        _, _, is_error, data, size = client.call_tool("asset_inspect", {"asset": sky["packageName"]})
        details = data or {}
        report.check("asset_inspect describes an asset from its package name",
                     not is_error and details.get("className") == "Blueprint" and details.get("diskSizeBytes", 0) > 0 and "ParentClass" in (details.get("tags") or {}),
                     f"{size} bytes, loaded={details.get('bLoaded')}, dependencies={details.get('dependencyCount')}, referencers={details.get('referencerCount')}")
        p1["asset_inspect"] = details

        _, _, is_error, data, _ = client.call_tool("asset_dependencies", {"asset": sky["path"]})
        dependencies = (data or {}).get("packages") or []
        report.check("asset_dependencies lists the packages a Blueprint uses", not is_error and len(dependencies) > 0,
                     ", ".join(f"{item['packageName']} ({item.get('className')}{', hard' if item.get('bHard') else ''})" for item in dependencies[:4]))
        p1["asset_dependencies"] = data
        if dependencies:
            dependency = dependencies[0]["packageName"]
            _, _, is_error, data, _ = client.call_tool("asset_referencers", {"asset": dependency, "limit": 500})
            referencers = [item["packageName"] for item in (data or {}).get("packages") or []]
            report.check("asset_referencers lists the Blueprint as a referencer of its dependency", not is_error and sky["packageName"] in referencers,
                         f"{dependency}: {len(referencers)} referencers")

        _, _, is_error, data, _ = client.call_tool("class_find_derived", {"baseClass": "Actor", "nameContains": "BP_Sky_Sphere"})
        sky_class = next((item for item in (data or {}).get("classes") or [] if item.get("name") == "BP_Sky_Sphere_C"), None)
        report.check("class_find_derived finds a Blueprint class with its asset", not is_error and sky_class is not None and sky_class.get("blueprint") == sky["path"],
                     json.dumps(sky_class) if sky_class else f"{error_code(data)} {error_message(data)[:120]}")

    _, _, is_error, data, _ = client.call_tool("class_find_derived", {"baseClass": "Widget", "nameContains": "TextBlock", "bIncludeBlueprint": False})
    text_block = next((item for item in (data or {}).get("classes") or [] if item.get("name") == "TextBlock"), None)
    report.check("class_find_derived reports the module and header of C++ classes",
                 not is_error and text_block is not None and text_block.get("module") == "/Script/UMG" and (text_block.get("header") or "").endswith("TextBlock.h"),
                 json.dumps(text_block) if text_block else f"{error_code(data)} {error_message(data)[:120]}")

    # --- blueprint_inspect ------------------------------------------------------------------------
    if sky:
        _, _, is_error, data, size = client.call_tool("blueprint_inspect", {"blueprint": sky["path"]})
        details = data or {}
        report.check("blueprint_inspect describes parents, components, variables and functions",
                     not is_error and (details.get("nativeParent") or {}).get("classPath") == "/Script/Engine.Actor"
                     and len(details.get("components") or []) > 0 and len(details.get("variables") or []) > 0 and len(details.get("functions") or []) > 0,
                     f"{size} bytes, status={details.get('status')}, components={len(details.get('components') or [])}, variables={len(details.get('variables') or [])}, "
                     f"functions={len(details.get('functions') or [])}, error={error_code(data)} {error_message(data)[:120]}")
        p1["blueprint_inspect"] = details

        summary_only = {"bIncludeComponents": False, "bIncludeVariables": False, "bIncludeFunctions": False}
        _, _, is_error, data, _ = client.call_tool("blueprint_inspect", dict(summary_only, blueprint=sky["path"] + "_C"))
        report.check("blueprint_inspect accepts the generated class path", not is_error and (data or {}).get("blueprint") == sky["path"], str((data or {}).get("blueprint") or error_code(data)))

        _, _, is_error, data, _ = client.call_tool("blueprint_inspect", dict(summary_only, blueprint=sky["packageName"]))
        report.check("blueprint_inspect accepts the package name", not is_error and (data or {}).get("blueprint") == sky["path"], str((data or {}).get("blueprint") or error_code(data)))

    # --- umg_inspect ------------------------------------------------------------------------------
    if widget_blueprint:
        _, _, is_error, data, size = client.call_tool("umg_inspect", {"widgetBlueprint": widget_blueprint["path"]})
        details = data or {}
        nodes = details.get("widgets") or []
        pre_order = bool(nodes) and nodes[0].get("depth") == 0 and all(node.get("depth") == 0 or node.get("parent") for node in nodes)
        report.check("umg_inspect lists the widget tree in pre-order with parents", not is_error and pre_order and details.get("widgetCount", 0) >= len(nodes),
                     f"{size} bytes, {len(nodes)} of {details.get('widgetCount')} widgets, parent={details.get('parentClass')}, error={error_code(data)} {error_message(data)[:120]}")
        report.info("umg_inspect bindings", f"bindWidgets={len(details.get('bindWidgets') or [])}, missing={details.get('missingBindWidgets')}, "
                                            f"animations={len(details.get('animations') or [])}, propertyBindings={len(details.get('propertyBindings') or [])}")
        p1["umg_inspect"] = details

        _, _, is_error, data, _ = client.call_tool("umg_inspect", {"widgetBlueprint": widget_blueprint["path"], "maxDepth": 0, "bIncludeSlots": False})
        report.check("umg_inspect stops at maxDepth", not is_error and len((data or {}).get("widgets") or []) == 1,
                     f"{len((data or {}).get('widgets') or [])} widgets, truncated={(data or {}).get('bTruncated')}")

        _, _, is_error, data, _ = client.call_tool("umg_inspect", {"widgetBlueprint": widget_blueprint["path"], "rootWidget": "NoSuchWidget_AgentMcp"})
        report.check("umg_inspect with an unknown rootWidget returns NOT_FOUND", is_error and error_code(data) == "NOT_FOUND", str(error_code(data)))

    # --- datatable --------------------------------------------------------------------------------
    table_checked = False
    for table in tables[:5]:
        _, _, is_error, schema, size = client.call_tool("datatable_get_schema", {"dataTable": table["path"]})
        if is_error:
            report.info("datatable_get_schema skipped a table", f"{table['path']}: {error_code(schema)} {error_message(schema)[:120]}")
            continue
        columns = (schema or {}).get("columns") or []
        report.check("datatable_get_schema lists columns with C++ types and JSON schemas",
                     len(columns) > 0 and all(column.get("type") and column.get("jsonSchema") for column in columns),
                     f"{table['path']}: {len(columns)} columns, rowStruct={(schema or {}).get('rowStruct')}, header={(schema or {}).get('rowStructHeader')}, {size} bytes")
        p1["datatable_get_schema"] = schema

        _, _, is_error, data, _ = client.call_tool("datatable_list_rows", {"dataTable": table["path"], "limit": 5})
        rows = (data or {}).get("rows") or []
        report.check("datatable_list_rows lists row names", not is_error and (data or {}).get("rowCount", 0) >= len(rows), f"{(data or {}).get('rowCount')} rows, first: {rows[:3]}")

        if rows:
            requested = rows[:2] + ["NoSuchRow_AgentMcp"]
            _, _, is_error, data, size = client.call_tool("datatable_get_rows", {"dataTable": table["path"], "rowNames": requested})
            values = (data or {}).get("rows") or {}
            first_row = values.get(rows[0]) or {}
            report.check("datatable_get_rows returns values by row and column and lists missing rows",
                         not is_error and all(name in values for name in rows[:2]) and "NoSuchRow_AgentMcp" in ((data or {}).get("missingRows") or [])
                         and all(column.get("name") in first_row for column in columns),
                         f"{size} bytes, first row: {json.dumps(first_row)[:160]}")
            p1["datatable_get_rows"] = data
        table_checked = True
        break
    if not table_checked:
        report.info("datatable checks", "no DataTable with a loaded row struct was found in mounted content")


P2_TOOLS = {
    "datatable_set_rows", "datatable_add_rows", "datatable_rename_rows", "datatable_remove_rows", "asset_save",
    "testbed_reset_fixtures",
}


def run_p2(client, report, evidence):
    p2 = evidence.setdefault("p2", {})
    _, _, is_error, data, _ = client.call_tool("testbed_reset_fixtures")
    table = (data or {}).get("dataTable")
    p2["fixtures"] = data
    if not report.check("testbed fixtures are reset and saved", not is_error and bool(table) and (data or {}).get("bSaved") is True,
                        json.dumps(data) if data else f"{error_code(data)} {error_message(data)}"):
        return

    def read_rows(names):
        _, _, failed, result, _ = client.call_tool("datatable_get_rows", {"dataTable": table, "rowNames": names})
        return {} if failed else ((result or {}).get("rows") or {})

    def list_rows():
        _, _, failed, result, _ = client.call_tool("datatable_list_rows", {"dataTable": table})
        return [] if failed else ((result or {}).get("rows") or [])

    baseline = read_rows(["Alpha", "Beta"])
    report.check("the fixture DataTable starts with baseline rows",
                 (baseline.get("Alpha") or {}).get("Count") == 1 and (baseline.get("Beta") or {}).get("Keywords") == [], json.dumps(baseline)[:240])

    # --- set_rows ---------------------------------------------------------------------------------
    undoable = get_undo_state(client).get("undoableCount") or 0
    _, _, is_error, data, _ = client.call_tool("datatable_set_rows", {"dataTable": table, "rows": {"Alpha": {"Count": 42, "Offset": {"z": 99}}, "Beta": {"Keywords": ["x", "y"]}}})
    p2["datatable_set_rows"] = data
    after = (data or {}).get("after") or {}
    alpha = after.get("Alpha") or {}
    report.check("datatable_set_rows changes only the given columns and struct fields",
                 not is_error and alpha.get("Count") == 42 and alpha.get("Offset") == {"x": 1, "y": 2, "z": 99} and alpha.get("Label") == "First"
                 and (after.get("Beta") or {}).get("Keywords") == ["x", "y"], json.dumps(data)[:320])
    report.check("datatable_set_rows is recorded as one undoable transaction",
                 ((data or {}).get("undo") or {}).get("recorded") is True and get_undo_state(client).get("undoableCount") == undoable + 1, json.dumps((data or {}).get("undo")))

    client.call_tool("editor_undo")
    restored = read_rows(["Alpha", "Beta"])
    report.check("editor_undo restores the DataTable rows", restored == baseline, json.dumps(restored)[:240])

    undoable = get_undo_state(client).get("undoableCount") or 0
    _, _, is_error, data, _ = client.call_tool("datatable_set_rows", {"dataTable": table, "rows": {"NoSuchRow": {"Count": 1}, "Alpha": {"NoSuchColumn": 1, "Count": "many"}}})
    message = error_message(data)
    report.check("invalid rows, columns and values are reported together and nothing changes",
                 is_error and all(text in message for text in ("NoSuchRow", "NoSuchColumn", "Alpha.Count"))
                 and read_rows(["Alpha"]).get("Alpha") == baseline.get("Alpha") and (get_undo_state(client).get("undoableCount") or 0) == undoable, message[:320])

    # --- add_rows, rename_rows --------------------------------------------------------------------
    _, _, is_error, data, _ = client.call_tool("datatable_add_rows", {"dataTable": table, "rowNames": ["Delta"], "values": {"Delta": {"Label": "Fourth", "Count": 4}}})
    report.check("datatable_add_rows adds a row with values",
                 not is_error and (data or {}).get("rowCount") == 4 and (((data or {}).get("after") or {}).get("Delta") or {}).get("Label") == "Fourth", json.dumps(data)[:240])

    _, _, is_error, data, _ = client.call_tool("datatable_add_rows", {"dataTable": table, "rowNames": ["Alpha"]})
    report.check("datatable_add_rows refuses an existing row name", is_error and error_code(data) == "ROW_EXISTS", str(error_code(data)))

    _, _, is_error, data, _ = client.call_tool("datatable_rename_rows", {"dataTable": table, "renames": {"Delta": "Epsilon"}})
    names = list_rows()
    report.check("datatable_rename_rows renames a row", not is_error and "Epsilon" in names and "Delta" not in names, ", ".join(names))

    # --- remove_rows: dry run, confirm, undo ------------------------------------------------------
    undoable = get_undo_state(client).get("undoableCount") or 0
    _, _, is_error, data, _ = client.call_tool("datatable_remove_rows", {"dataTable": table, "rowNames": ["Epsilon"]})
    report.check("datatable_remove_rows without bConfirm is a dry run",
                 not is_error and (data or {}).get("bApplied") is False and "Epsilon" in list_rows() and (get_undo_state(client).get("undoableCount") or 0) == undoable,
                 json.dumps(data)[:240])

    _, _, is_error, data, _ = client.call_tool("datatable_remove_rows", {"dataTable": table, "rowNames": ["Epsilon"], "bConfirm": True})
    report.check("datatable_remove_rows with bConfirm removes the row", not is_error and (data or {}).get("bApplied") is True and "Epsilon" not in list_rows(), json.dumps(data)[:240])

    client.call_tool("editor_undo")
    names = list_rows()
    report.check("editor_undo restores the removed row", "Epsilon" in names, ", ".join(names))

    engine_table = ((evidence.get("p1") or {}).get("datatable_get_schema") or {}).get("dataTable")
    if engine_table:
        _, _, is_error, data, _ = client.call_tool("datatable_set_rows", {"dataTable": engine_table, "rows": {"Any": {}}})
        report.check("DataTable edits of engine content return NOT_SUPPORTED", is_error and error_code(data) == "NOT_SUPPORTED", str(error_code(data)))

    # --- asset_save -------------------------------------------------------------------------------
    _, _, is_error, data, _ = client.call_tool("asset_inspect", {"asset": table})
    report.check("the edited DataTable is dirty before saving", not is_error and (data or {}).get("bDirty") is True, str((data or {}).get("bDirty")))

    _, _, is_error, data, _ = client.call_tool("asset_save", {"assets": [table]})
    p2["asset_save"] = data
    saved = (data or {}).get("packages") or [{}]
    report.check("asset_save saves the DataTable package", not is_error and (data or {}).get("savedCount") == 1 and saved[0].get("status") == "Saved",
                 json.dumps(data)[:240] if data else f"{error_code(data)} {error_message(data)}")

    _, _, is_error, data, _ = client.call_tool("asset_inspect", {"asset": table})
    report.check("the saved DataTable is no longer dirty", not is_error and (data or {}).get("bDirty") is False,
                 json.dumps({key: (data or {}).get(key) for key in ("bDirty", "diskSizeBytes")}))

    _, _, is_error, data, _ = client.call_tool("asset_save", {"assets": [table]})
    report.check("asset_save reports packages without changes as NotDirty", not is_error and (((data or {}).get("packages") or [{}])[0]).get("status") == "NotDirty", json.dumps(data)[:200])

    _, _, is_error, data, _ = client.call_tool("asset_save", {"assets": ["/Engine/EngineSky/BP_Sky_Sphere"]})
    report.check("asset_save refuses engine content", is_error and error_code(data) == "SAVE_REFUSED", error_message(data)[:200])

    # The checks above saved an edited table; leave the saved fixtures at their baseline for later sections and runs.
    _, _, is_error, data, _ = client.call_tool("testbed_reset_fixtures")
    report.check("testbed fixtures are restored after the DataTable checks", not is_error and (data or {}).get("bSaved") is True
                 and (data or {}).get("rows") == ["Alpha", "Beta", "Gamma"], json.dumps(data)[:200] if data else f"{error_code(data)} {error_message(data)}")


P3_TOOLS = {
    "blueprint_compile", "viewport_capture", "livecoding_compile",
    "testbed_write_then_fail", "testbed_wait_seconds", "testbed_set_pie_warning",
}


def call_with_cancel(client, name, arguments, cancel_after_seconds):
    """Starts a tools/call on a thread, sends notifications/cancelled for it, and returns (response, cancel status, seconds)."""
    request_id = client.next_id
    client.next_id += 1
    message = {"jsonrpc": "2.0", "id": request_id, "method": "tools/call", "params": {"name": name, "arguments": arguments}}
    outcome = {}

    def worker():
        outcome["response"] = client.send(message)

    started = time.time()
    thread = threading.Thread(target=worker)
    thread.start()
    time.sleep(cancel_after_seconds)
    cancel_status, _, _, _ = client.send({"jsonrpc": "2.0", "method": "notifications/cancelled", "params": {"requestId": request_id, "reason": "smoke test"}})
    thread.join(timeout=120)
    return outcome.get("response"), cancel_status, time.time() - started


def image_items(payload):
    content = ((payload or {}).get("result") or {}).get("content") or []
    return [item for item in content if item.get("type") == "image"]


def decode_png(data):
    """Returns (width, height, channels, pixel bytes) of an 8-bit, non-interlaced RGB or RGBA PNG."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    position, header, compressed = 8, None, b""
    while position < len(data):
        length = int.from_bytes(data[position:position + 4], "big")
        kind = data[position + 4:position + 8]
        body = data[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            header = body
        elif kind == b"IDAT":
            compressed += body
        elif kind == b"IEND":
            break
    width, height = int.from_bytes(header[0:4], "big"), int.from_bytes(header[4:8], "big")
    bit_depth, color_type, interlace = header[8], header[9], header[12]
    if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise ValueError(f"unsupported PNG: bit depth {bit_depth}, color type {color_type}, interlace {interlace}")
    channels = 4 if color_type == 6 else 3
    raw = zlib.decompress(compressed)
    stride = width * channels
    pixels = bytearray(height * stride)
    previous = bytearray(stride)
    for row in range(height):
        start = row * (stride + 1)
        filter_type = raw[start]
        line = bytearray(raw[start + 1:start + 1 + stride])
        if filter_type:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                up = previous[i]
                if filter_type == 1:
                    line[i] = (line[i] + left) & 0xFF
                elif filter_type == 2:
                    line[i] = (line[i] + up) & 0xFF
                elif filter_type == 3:
                    line[i] = (line[i] + ((left + up) >> 1)) & 0xFF
                else:
                    upper_left = previous[i - channels] if i >= channels else 0
                    estimate = left + up - upper_left
                    to_left, to_up, to_upper_left = abs(estimate - left), abs(estimate - up), abs(estimate - upper_left)
                    predictor = left if to_left <= to_up and to_left <= to_upper_left else (up if to_up <= to_upper_left else upper_left)
                    line[i] = (line[i] + predictor) & 0xFF
        pixels[row * stride:(row + 1) * stride] = line
        previous = line
    return width, height, channels, pixels


def count_probe_pixels(images):
    """Counts the magenta pixels of the fixture HUD's UiProbe image in the first image item (-1 without an image)."""
    if not images:
        return -1
    width, height, channels, pixels = decode_png(base64.b64decode(images[0].get("data", "")))
    return sum(1 for offset in range(0, width * height * channels, channels)
               if pixels[offset] > 200 and pixels[offset + 1] < 70 and pixels[offset + 2] > 200)


def run_p3(client, report, evidence):
    p3 = evidence.setdefault("p3", {})
    fixtures = (evidence.get("p2") or {}).get("fixtures") or {}
    if not fixtures.get("widgetBlueprints"):
        _, _, is_error, data, _ = client.call_tool("testbed_reset_fixtures")
        fixtures = {} if is_error else (data or {})
    widget_blueprints = fixtures.get("widgetBlueprints") or []
    bound = next((path for path in widget_blueprints if "WBP_AgentMcpBound" in path), None)
    missing = next((path for path in widget_blueprints if "WBP_AgentMcpMissingBinding" in path), None)
    report.check("testbed Widget Blueprint fixtures exist", bool(bound) and bool(missing), ", ".join(widget_blueprints))

    # --- blueprint_compile and BindWidget ---------------------------------------------------------
    if bound:
        _, _, is_error, data, _ = client.call_tool("blueprint_compile", {"blueprint": bound})
        report.check("blueprint_compile reports a clean Widget Blueprint as up to date",
                     not is_error and (data or {}).get("status") in ("UpToDate", "UpToDateWithWarnings") and (data or {}).get("errorCount") == 0, json.dumps(data)[:240])

        _, _, is_error, data, _ = client.call_tool("umg_inspect", {"widgetBlueprint": bound})
        bindings = {item.get("property"): item for item in (data or {}).get("bindWidgets") or []}
        title = bindings.get("TitleText") or {}
        report.check("umg_inspect reports a bound required BindWidget and an unbound optional one",
                     not is_error and title.get("bBound") is True and title.get("bTypeMatches") is True
                     and (bindings.get("Icon") or {}).get("bOptional") is True and not (data or {}).get("missingBindWidgets"),
                     json.dumps((data or {}).get("bindWidgets")))
        p3["umg_inspect_bound"] = data

    if missing:
        _, _, is_error, data, _ = client.call_tool("blueprint_compile", {"blueprint": missing})
        errors = [item for item in (data or {}).get("messages") or [] if item.get("severity") == "Error"]
        report.check("blueprint_compile returns the compile error of a missing BindWidget once",
                     not is_error and (data or {}).get("status") == "Error" and (data or {}).get("errorCount") == 1 and len(errors) == 1
                     and "TitleText" in errors[0].get("message", ""), json.dumps(data, ensure_ascii=False)[:320])
        p3["blueprint_compile_missing"] = data

        _, _, is_error, data, _ = client.call_tool("umg_inspect", {"widgetBlueprint": missing})
        report.check("umg_inspect lists the missing required BindWidget", not is_error and "TitleText" in ((data or {}).get("missingBindWidgets") or []),
                     json.dumps((data or {}).get("missingBindWidgets")))

    _, _, is_error, data, _ = client.call_tool("class_find_derived", {"baseClass": "AgentMcpTestbedWidget"})
    names = [item.get("name") for item in (data or {}).get("classes") or []]
    report.check("class_find_derived finds the Widget Blueprints of a C++ widget class",
                 not is_error and "WBP_AgentMcpBound_C" in names and "WBP_AgentMcpMissingBinding_C" in names, ", ".join(str(name) for name in names))

    # --- PIE refuses Blueprints with compile errors -------------------------------------------------
    if missing:
        _, _, is_error, data, _ = client.call_tool("pie_start", {"warmupSeconds": 0.5})
        report.check("pie_start refuses to start while a Blueprint has compile errors", is_error and error_code(data) == "BLUEPRINT_COMPILE_ERRORS", error_message(data)[:200])
        if not is_error:
            client.call_tool("pie_stop")
        client.call_tool("testbed_set_pie_warning", {"blueprint": missing, "bEnabled": False})

    # --- viewport_capture in the editor -------------------------------------------------------------
    _, payload, is_error, data, _ = client.call_tool("viewport_capture", {"maxWidth": 640, "bPreferPlay": False})
    images = image_items(payload)
    image_bytes = base64.b64decode(images[0].get("data", "")) if images else b""
    report.check("viewport_capture returns a PNG of the editor viewport and saves it",
                 not is_error and (data or {}).get("source") == "Editor" and len(images) == 1 and images[0].get("mimeType") == "image/png"
                 and image_bytes[:4] == b"\x89PNG" and 0 < (data or {}).get("width", 0) <= 640 and os.path.exists((data or {}).get("filePath") or ""),
                 f"{len(image_bytes)} PNG bytes, {json.dumps(data)[:240]}")
    p3["viewport_capture_editor"] = data

    # --- PIE with the fixture HUD: log and capture --------------------------------------------------
    _, _, is_error, data, _ = client.call_tool("pie_start", {"warmupSeconds": 1.5})
    if report.check("pie_start runs the fixture game mode", not is_error, error_message(data)[:200] if is_error else json.dumps((data or {}).get("playSession"))):
        start_sequence = (data or {}).get("startLogSequence", 0)
        _, _, _, logs, _ = client.call_tool("log_get_recent", {"sinceSequence": start_sequence, "contains": "AgentMcp testbed HUD", "maxEntries": 10})
        lines = [entry.get("message", "") for entry in (logs or {}).get("entries") or []]
        report.check("the play session log shows the fixture HUD", any("HUD shown" in line for line in lines), "; ".join(lines)[:240])

        _, payload, is_error, data, _ = client.call_tool("viewport_capture", {"maxWidth": 800})
        probe_pixels = count_probe_pixels(image_items(payload))
        report.check("viewport_capture captures the play session viewport with the game UI",
                     not is_error and (data or {}).get("source") == "Play" and (data or {}).get("bIncludesUI") is True and probe_pixels >= 1000,
                     f"{probe_pixels} UI probe pixels, {json.dumps(data)[:200]}")
        p3["viewport_capture_play"] = data

        _, payload, is_error, data, _ = client.call_tool("viewport_capture", {"maxWidth": 800, "bIncludeUI": False})
        probe_pixels = count_probe_pixels(image_items(payload))
        report.check("viewport_capture with bIncludeUI false reads only the scene",
                     not is_error and (data or {}).get("bIncludesUI") is False and 0 <= probe_pixels < 50, f"{probe_pixels} UI probe pixels")

        _, _, is_error, data, _ = client.call_tool("pie_stop")
        report.check("pie_stop ends the session after the capture", not is_error and (data or {}).get("bWasActive") is True, json.dumps(data)[:160])

    # --- dispatcher rollback of a failed write ------------------------------------------------------
    undoable = get_undo_state(client).get("undoableCount") or 0
    tags_before = read_property(client, "Floor", "Tags")
    _, _, is_error, data, _ = client.call_tool("testbed_write_then_fail", {"actor": "Floor"})
    tags_after = read_property(client, "Floor", "Tags")
    report.check("a failing Write tool rolls back its partial change",
                 is_error and error_code(data) == "TESTBED_FAILURE" and tags_after == tags_before and (get_undo_state(client).get("undoableCount") or 0) == undoable,
                 f"code={error_code(data)}, tags before={tags_before}, after={tags_after}")

    # --- cancellation of an asynchronous call -------------------------------------------------------
    response, cancel_status, elapsed = call_with_cancel(client, "testbed_wait_seconds", {"seconds": 20}, 1.0)
    payload = response[2] if response else None
    rpc_error = ((payload or {}).get("error") or {}).get("code")
    result = (payload or {}).get("result") or {}
    text = "".join(item.get("text", "") for item in result.get("content", []) if item.get("type") == "text")
    report.check("notifications/cancelled ends a pending asynchronous call",
                 cancel_status == 202 and elapsed < 10 and (rpc_error == -32800 or (result.get("isError") and "CANCELLED" in text)),
                 f"cancel status={cancel_status}, {elapsed:.1f}s, rpc error={rpc_error}, text={text[:120]}")

    # --- Live Coding ----------------------------------------------------------------------------------
    _, _, is_error, data, _ = client.call_tool("livecoding_compile")
    p3["livecoding_compile"] = data
    if is_error and error_code(data) == "NOT_AVAILABLE":
        report.info("livecoding_compile", error_message(data)[:200])
    else:
        report.check("livecoding_compile finishes with a result", not is_error and (data or {}).get("result") in ("NoChanges", "Success"), json.dumps(data)[:200])


P4_TOOLS = {"umg_create_widget_blueprint", "umg_add_widgets", "umg_set_widget_properties", "umg_remove_widgets"}
AUTHORING_WIDGET = "/Game/AgentMcpFixtures/WBP_AgentMcpAuthoring"


def widget_nodes(client, widget_blueprint, include_properties=False):
    """umg_inspect widget nodes by name; empty when the call fails."""
    _, _, is_error, data, _ = client.call_tool("umg_inspect", {"widgetBlueprint": widget_blueprint, "bIncludeProperties": include_properties})
    return {} if is_error else {node.get("name"): node for node in (data or {}).get("widgets") or []}


def run_p4(client, report, evidence):
    p4 = evidence.setdefault("p4", {})
    _, _, is_error, data, _ = client.call_tool("testbed_reset_fixtures")
    fixtures = {} if is_error else (data or {})
    report.check("testbed_reset_fixtures prepares the Widget Blueprint authoring checks", not is_error, json.dumps(fixtures.get("deleted")))
    missing_binding = next((path for path in fixtures.get("widgetBlueprints") or [] if "WBP_AgentMcpMissingBinding" in path), None)

    # --- umg_create_widget_blueprint ------------------------------------------------------------------
    _, _, _, data, _ = client.call_tool("log_get_recent", {"maxEntries": 1})
    log_start = (data or {}).get("nextSequence", 0)
    _, _, is_error, data, _ = client.call_tool("umg_create_widget_blueprint", {
        "assetPath": AUTHORING_WIDGET, "parentClass": "AgentMcpTestbedWidget", "rootWidgetClass": "CanvasPanel"})
    created = data or {}
    wbp = created.get("widgetBlueprint")
    root = created.get("rootWidget")
    report.check("umg_create_widget_blueprint creates a Widget Blueprint with a root panel and lists its unbound BindWidget",
                 not is_error and bool(wbp) and bool(root) and created.get("rootWidgetClass") == "CanvasPanel"
                 and created.get("missingBindWidgets") == ["TitleText"], json.dumps(data)[:300])
    p4["create"] = data
    if is_error or not wbp or not root:
        return

    _, _, is_error, data, _ = client.call_tool("umg_create_widget_blueprint", {"assetPath": AUTHORING_WIDGET})
    report.check("umg_create_widget_blueprint refuses a path that already has an asset", is_error and error_code(data) == "ASSET_EXISTS", error_message(data)[:200])

    _, _, is_error, data, _ = client.call_tool("umg_create_widget_blueprint", {"assetPath": "/Engine/AgentMcpSmoke/WBP_NotAllowed"})
    report.check("umg_create_widget_blueprint refuses engine content", is_error and error_code(data) == "NOT_SUPPORTED", error_message(data)[:200])

    # --- umg_add_widgets ------------------------------------------------------------------------------
    subtree = [{
        "class": "Border", "name": "Panel",
        "slot": {"LayoutData": {"Offsets": {"Left": 40, "Top": 40, "Right": 480, "Bottom": 200}}},
        "properties": {"BrushColor": {"R": 0.05, "G": 0.1, "B": 0.2, "A": 0.9}},
        "children": [{"class": "VerticalBox", "name": "Rows", "children": [
            {"class": "TextBlock", "name": "TitleText", "properties": {"Text": "Dungeon"}, "slot": {"Padding": {"Bottom": 8}}},
            {"class": "ProgressBar", "name": "Progress", "isVariable": True, "properties": {"Percent": 0.25}},
        ]}],
    }]
    _, _, is_error, data, _ = client.call_tool("umg_add_widgets", {"widgetBlueprint": wbp, "parent": root, "widgets": subtree})
    added = [node.get("name") for node in (data or {}).get("widgets") or []]
    report.check("umg_add_widgets adds a nested subtree in one undoable call and satisfies the BindWidget",
                 not is_error and added == ["Panel", "Rows", "TitleText", "Progress"] and (data or {}).get("bApplied") is True
                 and not (data or {}).get("missingBindWidgets") and ((data or {}).get("undo") or {}).get("recorded") is True,
                 json.dumps(data)[:400])
    p4["add"] = data

    # UClass::TryFindTypeSlow logged a warning with a callstack for each class name without a path, such as CanvasPanel and TextBlock above.
    _, _, _, data, _ = client.call_tool("log_get_recent", {"sinceSequence": log_start, "minVerbosity": "Warning", "contains": "Short type name"})
    short_name_warnings = (data or {}).get("entries") or []
    report.check("class names without a path resolve without log warnings", not short_name_warnings,
                 (short_name_warnings[0].get("message") or "")[:160] if short_name_warnings else "")

    nodes = widget_nodes(client, wbp, include_properties=True)
    title = nodes.get("TitleText") or {}
    # Property names keep their case in results; the fields of struct values come back in camelCase.
    title_padding = ((title.get("slot") or {}).get("properties") or {}).get("Padding") or {}
    report.check("umg_inspect shows the added widgets with their parents, slots and properties",
                 (nodes.get("Panel") or {}).get("parent") == root and (nodes.get("Rows") or {}).get("parent") == "Panel"
                 and title.get("parent") == "Rows" and (title.get("slot") or {}).get("className") == "VerticalBoxSlot"
                 and title_padding.get("bottom") == 8 and (title.get("properties") or {}).get("Text") == "Dungeon"
                 and (nodes.get("Progress") or {}).get("bIsVariable") is True,
                 json.dumps({name: nodes.get(name) for name in ("Panel", "TitleText", "Progress")})[:500])

    _, _, is_error, data, _ = client.call_tool("blueprint_compile", {"blueprint": wbp})
    report.check("the authored Widget Blueprint compiles without errors",
                 not is_error and (data or {}).get("errorCount") == 0 and (data or {}).get("status") in ("UpToDate", "UpToDateWithWarnings"),
                 json.dumps(data)[:300])

    count = len(widget_nodes(client, wbp))
    bad_entries = [
        {"class": "TextBlock", "name": "Extra", "properties": {"NoSuchProperty": 1}},
        {"class": "NoSuchWidgetClass"},
        {"class": "Image", "name": "TitleText"},
        {"class": "TextBlock", "children": [{"class": "Spacer"}]},
    ]
    _, _, is_error, data, _ = client.call_tool("umg_add_widgets", {"widgetBlueprint": wbp, "parent": "Rows", "widgets": bad_entries})
    message = error_message(data)
    report.check("umg_add_widgets reports every bad entry at once and changes nothing",
                 is_error and all(text in message for text in ("NoSuchProperty", "NoSuchWidgetClass", "TitleText", "cannot have children"))
                 and len(widget_nodes(client, wbp)) == count, message[:400])

    _, _, is_error, data, _ = client.call_tool("umg_add_widgets", {"widgetBlueprint": wbp, "parent": "Panel", "widgets": [{"class": "Spacer"}]})
    report.check("umg_add_widgets refuses a second child for a panel that holds one",
                 is_error and error_code(data) == "INVALID_ARGUMENT" and len(widget_nodes(client, wbp)) == count, error_message(data)[:200])

    # --- umg_set_widget_properties --------------------------------------------------------------------
    def title_text():
        return ((widget_nodes(client, wbp, include_properties=True).get("TitleText") or {}).get("properties") or {}).get("Text")

    _, _, is_error, data, _ = client.call_tool("umg_set_widget_properties", {"widgetBlueprint": wbp, "widgets": {
        "TitleText": {"properties": {"Text": "Cleared"}, "slot": {"Padding": {"Top": 4}}},
        "Progress": {"properties": {"Percent": 1.0}, "isVariable": False},
    }})
    changed = {node.get("name"): node for node in (data or {}).get("widgets") or []}
    title = changed.get("TitleText") or {}
    progress = changed.get("Progress") or {}
    padding = ((title.get("slot") or {}).get("properties") or {}).get("Padding") or {}
    # The result reads back only the requested fields; umg_inspect shows that the other Padding fields kept their values.
    inspected_padding = (((widget_nodes(client, wbp).get("TitleText") or {}).get("slot") or {}).get("properties") or {}).get("Padding") or {}
    report.check("umg_set_widget_properties changes widget values, single slot fields and isVariable of several widgets",
                 not is_error and (title.get("properties") or {}).get("Text") == "Cleared" and padding == {"top": 4}
                 and inspected_padding.get("top") == 4 and inspected_padding.get("bottom") == 8
                 and (progress.get("properties") or {}).get("Percent") == 1.0 and not progress.get("bIsVariable"), json.dumps(data)[:400])
    p4["set"] = data

    _, _, is_error, data, _ = client.call_tool("umg_set_widget_properties", {"widgetBlueprint": wbp, "widgets": {
        "TitleText": {"properties": {"Text": "Not applied"}}, "NoSuchWidget": {"properties": {"Text": "x"}}}})
    current = title_text()
    report.check("umg_set_widget_properties changes nothing when one widget does not exist",
                 is_error and error_code(data) == "NOT_FOUND" and current == "Cleared", f"code={error_code(data)}, Text={current}")

    _, _, undo_error, _, _ = client.call_tool("editor_undo")
    after_undo = title_text()
    _, _, redo_error, _, _ = client.call_tool("editor_redo")
    after_redo = title_text()
    report.check("editor_undo and editor_redo revert and reapply umg_set_widget_properties",
                 not undo_error and not redo_error and after_undo == "Dungeon" and after_redo == "Cleared", f"after undo={after_undo}, after redo={after_redo}")

    _, _, is_error, _, _ = client.call_tool("umg_add_widgets", {"widgetBlueprint": wbp, "parent": root, "widgets": [{"class": "Image", "name": "Badge"}]})
    added_badge = not is_error and "Badge" in widget_nodes(client, wbp)
    client.call_tool("editor_undo")
    undone = "Badge" not in widget_nodes(client, wbp)
    _, _, is_error, data, _ = client.call_tool("umg_add_widgets", {"widgetBlueprint": wbp, "parent": root, "widgets": [{"class": "SizeBox", "name": "Badge"}]})
    badge = widget_nodes(client, wbp).get("Badge") or {}
    report.check("after editor_undo of umg_add_widgets the widget name can be used again, also for another class",
                 added_badge and undone and not is_error and badge.get("className") == "SizeBox",
                 f"added={added_badge}, undone={undone}, class={badge.get('className')}, {error_message(data)[:160]}")

    # --- nested Widget Blueprints and entry classes ----------------------------------------------------
    part = AUTHORING_WIDGET + "Part"
    part_class = part + "." + part.rsplit("/", 1)[1] + "_C"
    _, _, is_error, data, _ = client.call_tool("umg_create_widget_blueprint", {"assetPath": part, "parentClass": "AgentMcpTestbedWidget", "rootWidgetClass": "Overlay"})
    part_root = (data or {}).get("rootWidget")
    _, _, add_error, added, _ = client.call_tool("umg_add_widgets", {"widgetBlueprint": part, "parent": part_root or "", "widgets": [{"class": "TextBlock", "name": "TitleText"}]})
    _, _, compile_error, compiled, _ = client.call_tool("blueprint_compile", {"blueprint": part})
    report.check("a component Widget Blueprint for nesting is created and compiles",
                 not is_error and bool(part_root) and not add_error and not compile_error and (compiled or {}).get("errorCount") == 0,
                 json.dumps(compiled)[:200] if not add_error else error_message(added)[:200])

    # Two Widget Blueprints with the same asset name in different folders share a class name, so that name alone is ambiguous.
    part_copy = FIXTURE_FOLDER + "/Copy/WBP_AgentMcpAuthoringPart"
    _, _, copy_error, _, _ = client.call_tool("umg_create_widget_blueprint", {"assetPath": part_copy})
    _, _, is_error, data, _ = client.call_tool("umg_add_widgets", {"widgetBlueprint": wbp, "parent": root, "widgets": [{"class": "WBP_AgentMcpAuthoringPart_C"}]})
    hint = ((data or {}).get("error") or {}).get("hint") or ""
    report.check("umg_add_widgets reports a class name of two Widget Blueprints as ambiguous and lists both classes",
                 not copy_error and is_error and error_code(data) == "AMBIGUOUS_REFERENCE" and part_class in hint and part_copy + "." in hint,
                 error_message(data)[:160] + " " + hint[:300])

    _, _, is_error, data, _ = client.call_tool("umg_add_widgets", {"widgetBlueprint": wbp, "parent": root, "widgets": [
        {"class": part_class, "name": "PartInstance", "properties": {"Caption": "Nested"}},
        {"class": "DynamicEntryBox", "name": "Entries", "properties": {"EntryWidgetClass": part_class, "NumDesignerPreviewEntries": 2}},
    ]})
    nested = {node.get("name"): node for node in (data or {}).get("widgets") or []}
    instance = nested.get("PartInstance") or {}
    entries = nested.get("Entries") or {}
    report.check("umg_add_widgets adds a Widget Blueprint instance with an instance property and sets a DynamicEntryBox entry class",
                 not is_error and instance.get("className") == part_class.rsplit(".", 1)[1] and instance.get("widgetClass") == part_class
                 and (instance.get("properties") or {}).get("Caption") == "Nested"
                 and (entries.get("properties") or {}).get("EntryWidgetClass") == part_class, json.dumps(data)[:400])

    _, _, is_error, data, _ = client.call_tool("blueprint_compile", {"blueprint": wbp})
    report.check("the Widget Blueprint with a nested instance compiles", not is_error and (data or {}).get("errorCount") == 0, json.dumps(data)[:200])

    _, _, is_error, data, _ = client.call_tool("umg_add_widgets", {"widgetBlueprint": wbp, "parent": root, "widgets": [{"class": wbp + "_C", "name": "Recursive"}]})
    report.check("umg_add_widgets refuses to nest a Widget Blueprint in itself", is_error and "contain itself" in error_message(data), error_message(data)[:200])

    # --- umg_remove_widgets ---------------------------------------------------------------------------
    count = len(widget_nodes(client, wbp))
    _, _, is_error, data, _ = client.call_tool("umg_remove_widgets", {"widgetBlueprint": wbp, "widgetNames": ["Rows", "Progress"]})
    listed = [node.get("name") for node in (data or {}).get("widgets") or []]
    warnings = " ".join((data or {}).get("warnings") or [])
    report.check("umg_remove_widgets without bConfirm lists the subtree once, warns about the BindWidget and removes nothing",
                 not is_error and not (data or {}).get("bApplied") and listed == ["Rows", "TitleText", "Progress"]
                 and "TitleText" in warnings and "Dry run" in warnings and len(widget_nodes(client, wbp)) == count, json.dumps(data)[:400])

    _, _, is_error, data, _ = client.call_tool("umg_remove_widgets", {"widgetBlueprint": wbp, "widgetNames": ["Rows"], "bConfirm": True})
    nodes = widget_nodes(client, wbp)
    report.check("umg_remove_widgets with bConfirm removes the subtree and lists the unbound BindWidget",
                 not is_error and (data or {}).get("bApplied") is True and "TitleText" in ((data or {}).get("missingBindWidgets") or [])
                 and not any(name in nodes for name in ("Rows", "TitleText", "Progress")), json.dumps(data)[:300])

    client.call_tool("editor_undo")
    nodes = widget_nodes(client, wbp)
    _, _, is_error, data, _ = client.call_tool("blueprint_compile", {"blueprint": wbp})
    parents = {name: (nodes.get(name) or {}).get("parent") for name in ("Rows", "TitleText")}
    report.check("editor_undo restores the removed widgets in place and the Widget Blueprint compiles again",
                 parents == {"Rows": "Panel", "TitleText": "Rows"} and not is_error and (data or {}).get("errorCount") == 0,
                 f"parents={json.dumps(parents)}, compile={json.dumps(data)[:200]}")

    # --- play session refusal -------------------------------------------------------------------------
    if missing_binding:
        client.call_tool("testbed_set_pie_warning", {"blueprint": missing_binding, "bEnabled": False})
    _, _, is_error, data, _ = client.call_tool("pie_start", {"warmupSeconds": 0.5})
    if report.check("pie_start runs for the play session refusal check", not is_error, error_message(data)[:200]):
        _, _, add_error, add_data, _ = client.call_tool("umg_add_widgets", {"widgetBlueprint": wbp, "parent": root, "widgets": [{"class": "Spacer"}]})
        _, _, create_error, create_data, _ = client.call_tool("umg_create_widget_blueprint", {"assetPath": AUTHORING_WIDGET + "_Pie"})
        client.call_tool("pie_stop")
        report.check("umg_add_widgets and umg_create_widget_blueprint are refused during a play session",
                     add_error and error_code(add_data) == "PIE_ACTIVE" and create_error and error_code(create_data) == "PIE_ACTIVE",
                     f"add={error_code(add_data)}, create={error_code(create_data)}")

    _, _, is_error, data, _ = client.call_tool("testbed_reset_fixtures")
    report.check("testbed_reset_fixtures deletes the authored Widget Blueprint",
                 not is_error and any("WBP_AgentMcpAuthoring" in path for path in (data or {}).get("deleted") or []), json.dumps((data or {}).get("deleted")))


P5_TOOLS = {"skills_list", "skills_get"}
# Config/DefaultEditorPerProjectUserSettings.ini of the testbed adds this folder to SkillDirectories.
SMOKE_SKILLS_FOLDER = "Saved/MCP/SmokeSkills"
PLUGIN_UMG_SKILL = "Plugins/AgentMcp/Skills/umg-authoring"


def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def normalized_path(path):
    return (path or "").replace("\\", "/").rstrip("/")


def list_skills(client):
    """The skills_list result and its skills by name; empty when the call fails."""
    _, _, is_error, data, _ = client.call_tool("skills_list")
    data = {} if is_error else (data or {})
    return data, {skill.get("name"): skill for skill in data.get("skills") or []}


def run_p5(client, report, evidence):
    p5 = evidence.setdefault("p5", {})
    instructions = (evidence.get("initialize") or {}).get("instructions") or ""
    skills_line = instructions[instructions.find("- Skills"):] if "- Skills" in instructions else ""
    report.check("initialize instructions list the skills and point to skills_get",
                 "skills_get" in skills_line and "umg-authoring" in skills_line, skills_line[:200])

    listing, skills = list_skills(client)
    p5["list"] = listing
    plugin_skill = skills.get("umg-authoring") or {}
    report.check("skills_list returns the plugin skill umg-authoring with its description and folder",
                 bool(plugin_skill.get("description")) and normalized_path(plugin_skill.get("path")).endswith(PLUGIN_UMG_SKILL),
                 json.dumps(plugin_skill)[:300])

    _, _, is_error, data, _ = client.call_tool("skills_get", {"name": "umg-authoring"})
    content = (data or {}).get("content") or ""
    report.check("skills_get returns the SKILL.md instructions without the front matter",
                 not is_error and (data or {}).get("file") == "SKILL.md" and content.startswith("# ") and "name: umg-authoring" not in content,
                 content[:120])

    _, _, is_error, data, _ = client.call_tool("skills_get", {"name": "no-such-skill"})
    hint = ((data or {}).get("error") or {}).get("hint") or ""
    report.check("skills_get with an unknown name returns NOT_FOUND and names the available skills",
                 is_error and error_code(data) == "NOT_FOUND" and "umg-authoring" in hint, hint[:200])

    root = next((path for path in listing.get("searchedDirectories") or [] if normalized_path(path).endswith(SMOKE_SKILLS_FOLDER)), None)
    if not report.check("skills_list searches the smoke test folder from SkillDirectories", bool(root), json.dumps(listing.get("searchedDirectories"))):
        return
    shutil.rmtree(root, ignore_errors=True)
    try:
        write_text(os.path.join(root, "smoke-guide", "SKILL.md"),
                   "---\nname: smoke-guide\ndescription: >\n  Smoke test skill whose description\n  is folded over two lines.\n---\n\n"
                   "# Smoke guide\n\nRead references/checklist.md.\n")
        write_text(os.path.join(root, "smoke-guide", "references", "checklist.md"), "- first\n- second\n")
        write_text(os.path.join(root, "smoke-guide", ".drafts", "note.md"), "hidden\n")
        write_text(os.path.join(root, "no-description", "SKILL.md"), "---\nname: no-description\n---\n\n# Body\n")
        write_text(os.path.join(root, "wrong-folder", "SKILL.md"), "---\nname: other-name\ndescription: The name differs from the folder.\n---\n\n# Body\n")
        write_text(os.path.join(root, "umg-authoring", "SKILL.md"),
                   "---\nname: umg-authoring\ndescription: \"Project rules: replace the plugin skill.\"\n---\n\n# Project UMG rules\n")

        listing, skills = list_skills(client)
        p5["listWithProjectSkills"] = listing
        guide = skills.get("smoke-guide") or {}
        report.check("skills_list reads a project skill with a folded description",
                     guide.get("description") == "Smoke test skill whose description is folded over two lines.", json.dumps(guide)[:300])
        problems = " | ".join(listing.get("problems") or [])
        report.check("skills_list skips and reports skill files without a description or whose name differs from the folder",
                     "no-description" in problems and "wrong-folder" in problems and "no-description" not in skills and "other-name" not in skills,
                     problems[:400])
        override = skills.get("umg-authoring") or {}
        report.check("a project skill replaces the plugin skill with the same name",
                     override.get("description") == "Project rules: replace the plugin skill."
                     and normalized_path(override.get("overrides")).endswith(PLUGIN_UMG_SKILL), json.dumps(override)[:300])

        _, _, is_error, data, _ = client.call_tool("skills_get", {"name": "smoke-guide"})
        report.check("skills_get lists the other files of a skill and leaves out hidden ones",
                     not is_error and (data or {}).get("files") == ["references/checklist.md"], json.dumps(data)[:300])

        _, _, is_error, data, _ = client.call_tool("skills_get", {"name": "smoke-guide", "file": "references/checklist.md"})
        report.check("skills_get reads a file of a skill",
                     not is_error and (data or {}).get("file") == "references/checklist.md" and (data or {}).get("content") == "- first\n- second\n",
                     json.dumps(data)[:200])

        _, _, is_error, data, _ = client.call_tool("skills_get", {"name": "smoke-guide", "file": "../umg-authoring/SKILL.md"})
        report.check("skills_get refuses a file outside the skill folder", is_error and error_code(data) == "INVALID_ARGUMENT", error_message(data)[:200])

        _, _, is_error, data, _ = client.call_tool("skills_get", {"name": "smoke-guide", "file": "references/missing.md"})
        report.check("skills_get reports a missing file as NOT_FOUND", is_error and error_code(data) == "NOT_FOUND", error_message(data)[:200])
    finally:
        shutil.rmtree(root, ignore_errors=True)

    _, skills = list_skills(client)
    report.check("skill files are read on every call: removing the project skills restores the plugin skill",
                 normalized_path((skills.get("umg-authoring") or {}).get("path")).endswith(PLUGIN_UMG_SKILL) and "smoke-guide" not in skills,
                 json.dumps(sorted(skills))[:200])


P6_TOOLS = {"asset_create", "asset_import_textures"}
FIXTURE_FOLDER = "/Game/AgentMcpFixtures"
SMOKE_DATA_ASSET = FIXTURE_FOLDER + "/DA_AgentMcpSmoke"
SMOKE_CREATED_TABLE = FIXTURE_FOLDER + "/DT_AgentMcpCreated"
SMOKE_TEXTURE = FIXTURE_FOLDER + "/T_AgentMcpSmokeIcon"


def object_path(package_name):
    return f"{package_name}.{package_name.rsplit('/', 1)[-1]}"


def write_png(path, width, height, rgba):
    """Writes a PNG filled with one RGBA color, with the standard library only."""
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    row = b"\x00" + bytes(rgba) * width
    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(row * height)) + chunk(b"IEND", b""))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data)


def project_folder(client):
    """The project folder of the editor: skills_list searches <project>/AgentMcp/Skills."""
    listing, _ = list_skills(client)
    for path in listing.get("searchedDirectories") or []:
        folder = normalized_path(path)
        if folder.endswith("/AgentMcp/Skills") and not folder.endswith("/Plugins/AgentMcp/Skills"):
            return folder[:-len("/AgentMcp/Skills")]
    return None


def run_p6(client, report, evidence):
    p6 = evidence.setdefault("p6", {})
    _, _, is_error, data, _ = client.call_tool("testbed_reset_fixtures")
    fixtures = {} if is_error else (data or {})
    report.check("testbed_reset_fixtures prepares the asset checks", not is_error, json.dumps(fixtures.get("deleted")))
    missing_binding = next((path for path in fixtures.get("widgetBlueprints") or [] if "WBP_AgentMcpMissingBinding" in path), None)
    bound_widget = next((path for path in fixtures.get("widgetBlueprints") or [] if "WBP_AgentMcpBound" in path), None)

    # --- asset_create ---------------------------------------------------------------------------------
    _, _, is_error, data, _ = client.call_tool("asset_create", {"assetPath": SMOKE_DATA_ASSET, "assetClass": "AgentMcpTestbedDataAsset"})
    report.check("asset_create creates a data asset",
                 not is_error and (data or {}).get("asset") == object_path(SMOKE_DATA_ASSET) and (data or {}).get("className") == "AgentMcpTestbedDataAsset",
                 json.dumps(data)[:200])
    p6["createDataAsset"] = data

    _, _, is_error, data, _ = client.call_tool("asset_create", {"assetPath": SMOKE_DATA_ASSET, "assetClass": "AgentMcpTestbedDataAsset"})
    report.check("asset_create refuses a path that already has an asset", is_error and error_code(data) == "ASSET_EXISTS", error_message(data)[:200])

    _, _, is_error, data, _ = client.call_tool("asset_create", {"assetPath": FIXTURE_FOLDER + "/DA_AgentMcpActor", "assetClass": "Actor"})
    report.check("asset_create refuses classes that are not data assets", is_error and error_code(data) == "NOT_SUPPORTED", error_message(data)[:200])

    _, _, is_error, data, _ = client.call_tool("asset_create", {"assetPath": "/Engine/AgentMcpSmoke/DA_NotAllowed", "assetClass": "AgentMcpTestbedDataAsset"})
    report.check("asset_create refuses engine content", is_error and error_code(data) == "NOT_SUPPORTED", error_message(data)[:200])

    _, _, is_error, data, _ = client.call_tool("asset_create", {"assetPath": SMOKE_CREATED_TABLE, "assetClass": "DataTable"})
    report.check("asset_create needs a row struct for a DataTable", is_error and error_code(data) == "INVALID_ARGUMENT", error_message(data)[:200])

    _, _, is_error, data, _ = client.call_tool("asset_create", {"assetPath": SMOKE_CREATED_TABLE, "assetClass": "DataTable", "rowStruct": "Vector"})
    report.check("asset_create refuses a struct that is not a row struct", is_error and error_code(data) == "INVALID_ARGUMENT", error_message(data)[:200])

    _, _, is_error, data, _ = client.call_tool("asset_create", {"assetPath": SMOKE_CREATED_TABLE, "assetClass": "DataTable", "rowStruct": "AgentMcpTestbedRow"})
    _, _, rows_error, rows, _ = client.call_tool("datatable_list_rows", {"dataTable": object_path(SMOKE_CREATED_TABLE)})
    report.check("asset_create creates an empty DataTable with the row struct",
                 not is_error and ((data or {}).get("rowStruct") or "").endswith(".AgentMcpTestbedRow") and not rows_error and (rows or {}).get("rowCount") == 0,
                 json.dumps(data)[:200])

    # --- object_set_properties on assets --------------------------------------------------------------
    data_asset = object_path(SMOKE_DATA_ASSET)
    _, _, is_error, data, _ = client.call_tool("object_set_properties", {"object": data_asset, "values": {"Count": 7, "Color": {"R": 1, "G": 0.5, "B": 0, "A": 1}}})
    after = (data or {}).get("after") or {}
    color = after.get("Color") or {}
    report.check("object_set_properties changes a data asset in one undoable call",
                 not is_error and after.get("Count") == 7 and abs((color.get("g", color.get("G")) or 0) - 0.5) < 0.01
                 and ((data or {}).get("undo") or {}).get("recorded") is True, json.dumps(data)[:300])

    client.call_tool("editor_undo")
    count_after_undo = read_property(client, data_asset, "Count")
    client.call_tool("editor_redo")
    count_after_redo = read_property(client, data_asset, "Count")
    report.check("editor_undo and editor_redo restore data asset values", count_after_undo == 0 and count_after_redo == 7,
                 f"after undo {count_after_undo}, after redo {count_after_redo}")

    _, _, is_error, data, _ = client.call_tool("object_set_properties", {"object": data_asset, "values": {"Tokens": {"LineStrong": 1, "ItemID": 2}}})
    tokens = ((data or {}).get("after") or {}).get("Tokens")
    report.check("map keys keep their case in results", not is_error and tokens == {"LineStrong": 1, "ItemID": 2}, json.dumps(tokens))

    _, _, is_error, data, _ = client.call_tool("object_set_properties", {"object": bound_widget, "values": {"BlueprintDescription": "x"}})
    report.check("object_set_properties refuses Blueprints", is_error and error_code(data) == "NOT_SUPPORTED", error_message(data)[:200])

    _, _, is_error, data, _ = client.call_tool("object_set_properties", {"object": fixtures.get("dataTable"), "values": {"bStripFromClientBuilds": True}})
    hint = ((data or {}).get("error") or {}).get("hint") or ""
    report.check("object_set_properties refuses DataTables and names the datatable tools",
                 is_error and error_code(data) == "NOT_SUPPORTED" and "datatable_set_rows" in hint, error_message(data)[:200])

    _, _, is_error, data, _ = client.call_tool("object_set_properties", {"object": "/Engine/EngineResources/DefaultTexture.DefaultTexture", "values": {"SRGB": False}})
    report.check("object_set_properties refuses engine assets", is_error and error_code(data) == "NOT_SUPPORTED", error_message(data)[:200])

    # --- asset_import_textures ------------------------------------------------------------------------
    project = project_folder(client)
    if not report.check("the project folder is known from skills_list", bool(project), str(project)):
        return
    import_folder = os.path.join(project, "Saved", "MCP", "SmokeImport")
    shutil.rmtree(import_folder, ignore_errors=True)
    write_png(os.path.join(import_folder, "smoke_icon.png"), 64, 32, (255, 128, 0, 255))
    large_icon = os.path.join(import_folder, "smoke_icon_large.png")
    write_png(large_icon, 128, 64, (0, 128, 255, 255))
    write_text(os.path.join(import_folder, "notes.txt"), "not an image\n")
    relative_icon = "Saved/MCP/SmokeImport/smoke_icon.png"

    try:
        _, _, is_error, data, _ = client.call_tool("asset_import_textures", {"textures": [
            {"file": relative_icon, "asset": SMOKE_TEXTURE},
            {"file": "Saved/MCP/SmokeImport/missing.png", "asset": FIXTURE_FOLDER + "/T_AgentMcpMissing"}]})
        _, _, inspect_error, _, _ = client.call_tool("asset_inspect", {"asset": SMOKE_TEXTURE})
        report.check("asset_import_textures checks every entry first and imports nothing when one is invalid",
                     is_error and error_code(data) == "NOT_FOUND" and inspect_error, error_message(data)[:200])

        _, _, is_error, data, _ = client.call_tool("asset_import_textures", {"textures": [{"file": "Saved/MCP/SmokeImport/notes.txt", "asset": SMOKE_TEXTURE}]})
        report.check("asset_import_textures refuses files that are not images", is_error and error_code(data) == "INVALID_ARGUMENT", error_message(data)[:200])

        _, _, is_error, data, _ = client.call_tool("asset_import_textures", {"textures": [{"file": relative_icon, "asset": "/Engine/AgentMcpSmoke/T_NotAllowed"}]})
        report.check("asset_import_textures refuses engine content", is_error and error_code(data) == "NOT_SUPPORTED", error_message(data)[:200])

        _, _, is_error, data, _ = client.call_tool("asset_import_textures", {"textures": [
            {"file": relative_icon, "asset": SMOKE_TEXTURE}, {"file": large_icon, "asset": SMOKE_TEXTURE}]})
        report.check("asset_import_textures refuses two entries for the same asset", is_error and error_code(data) == "INVALID_ARGUMENT", error_message(data)[:200])

        _, _, is_error, data, _ = client.call_tool("asset_import_textures", {"textures": [{"file": relative_icon, "asset": SMOKE_TEXTURE}]})
        imported = ((data or {}).get("textures") or [{}])[0]
        report.check("asset_import_textures imports a PNG from a path relative to the project",
                     not is_error and imported.get("asset") == object_path(SMOKE_TEXTURE) and imported.get("width") == 64 and imported.get("height") == 32,
                     json.dumps(data)[:300])
        p6["import"] = data

        texture = object_path(SMOKE_TEXTURE)
        _, _, is_error, data, _ = client.call_tool("object_get_properties", {"object": texture, "propertyNames": ["LODGroup", "MipGenSettings", "CompressionSettings", "SRGB"]})
        values = (data or {}).get("values") or {}
        report.check("imported textures get the settings for UMG",
                     not is_error and "UI" in str(values.get("LODGroup")) and "NoMipmaps" in str(values.get("MipGenSettings"))
                     and "EditorIcon" in str(values.get("CompressionSettings")) and values.get("SRGB") is True, json.dumps(values))

        _, _, is_error, data, _ = client.call_tool("asset_import_textures", {"textures": [{"file": relative_icon, "asset": SMOKE_TEXTURE}]})
        report.check("asset_import_textures refuses an existing asset without bReplaceExisting", is_error and error_code(data) == "ASSET_EXISTS", error_message(data)[:200])

        _, _, is_error, data, _ = client.call_tool("asset_import_textures", {"textures": [{"file": large_icon, "asset": SMOKE_TEXTURE}], "bReplaceExisting": True})
        replaced = ((data or {}).get("textures") or [{}])[0]
        report.check("asset_import_textures replaces a texture from an absolute path",
                     not is_error and replaced.get("bReplaced") is True and replaced.get("width") == 128 and replaced.get("height") == 64, json.dumps(data)[:300])

        _, _, is_error, data, _ = client.call_tool("asset_import_textures", {"textures": [{"file": large_icon, "asset": SMOKE_DATA_ASSET}], "bReplaceExisting": True})
        report.check("asset_import_textures does not replace assets that are not textures", is_error and error_code(data) == "ASSET_EXISTS", error_message(data)[:200])

        _, _, is_error, data, _ = client.call_tool("object_set_properties", {"object": data_asset, "values": {
            "Icon": texture, "Brush": {"ResourceObject": texture, "ImageSize": {"X": 128, "Y": 64}}}})
        report.check("object_set_properties points a data asset at the imported texture",
                     not is_error and json.dumps((data or {}).get("after")).count(texture) >= 2, json.dumps((data or {}).get("after"))[:300])

        _, _, is_error, data, _ = client.call_tool("asset_save", {"assets": [SMOKE_DATA_ASSET, SMOKE_CREATED_TABLE, SMOKE_TEXTURE]})
        report.check("asset_save saves the created and imported assets", not is_error and (data or {}).get("savedCount") == 3, json.dumps(data)[:300])

        # --- play session refusal -------------------------------------------------------------------------
        if missing_binding:
            client.call_tool("testbed_set_pie_warning", {"blueprint": missing_binding, "bEnabled": False})
        _, _, is_error, data, _ = client.call_tool("pie_start", {"warmupSeconds": 0.5})
        if report.check("pie_start runs for the asset tool refusal checks", not is_error, error_message(data)[:200]):
            _, _, create_error, create_data, _ = client.call_tool("asset_create", {"assetPath": FIXTURE_FOLDER + "/DA_AgentMcpDuringPlay", "assetClass": "AgentMcpTestbedDataAsset"})
            _, _, import_error, import_data, _ = client.call_tool("asset_import_textures", {"textures": [{"file": relative_icon, "asset": FIXTURE_FOLDER + "/T_AgentMcpDuringPlay"}]})
            client.call_tool("pie_stop")
            report.check("asset_create and asset_import_textures are refused during a play session",
                         create_error and error_code(create_data) == "PIE_ACTIVE" and import_error and error_code(import_data) == "PIE_ACTIVE",
                         f"{error_code(create_data)}, {error_code(import_data)}")
    finally:
        shutil.rmtree(import_folder, ignore_errors=True)


def run_protocol_errors(client, report):
    status, _, payload, _ = client.request("tools/call", {"name": "no_such_tool", "arguments": {}})
    report.check("unknown tool returns JSON-RPC -32602", ((payload or {}).get("error") or {}).get("code") == -32602, str((payload or {}).get("error")))

    status, _, payload, _ = client.request("no/such/method")
    report.check("unknown method returns JSON-RPC -32601", ((payload or {}).get("error") or {}).get("code") == -32601, str((payload or {}).get("error")))

    status, _, _, _ = client.request("tools/list", use_session=False)
    report.check("missing Mcp-Session-Id returns 400", status == 400, f"status={status}")

    status, _, _, _ = client.request("tools/list", use_session=False, extra_headers={"Mcp-Session-Id": "00000000-dead-beef-0000-000000000000"})
    report.check("unknown session returns 404", status == 404, f"status={status}")

    status, _, payload, _ = client.send(raw_body=b"{not json")
    report.check("invalid JSON returns 400 with -32700", status == 400 and ((payload or {}).get("error") or {}).get("code") == -32700, f"status={status}")

    status, _, _, _ = client.send([{"jsonrpc": "2.0", "id": 99, "method": "ping"}])
    report.check("batch request is rejected with 400", status == 400, f"status={status}")

    status, _, _, _ = client.request("ping", extra_headers={"Origin": "http://evil.example"})
    report.check("foreign Origin is rejected with 403", status == 403, f"status={status}")

    status, _, _, _ = client.send(method="GET")
    report.check("GET returns 405", status == 405, f"status={status}")

    status, _, _, _ = client.send(method="DELETE")
    report.check("DELETE ends the session", status == 200, f"status={status}")

    status, _, _, _ = client.request("ping")
    report.check("closed session returns 404", status == 404, f"status={status}")


def connected_project(url, token):
    """Returns the project of the editor serving url, using a separate session that is closed again (None if unknown)."""
    client = McpClient(url, token, timeout=30.0)
    status, headers, _, _ = client.request("initialize", {
        "protocolVersion": "2025-06-18",
        "capabilities": {},
        "clientInfo": {"name": "mcp_smoke.py preflight", "version": "1.0"},
    }, use_session=False)
    if status != 200 or not headers or not headers.get("Mcp-Session-Id"):
        return None
    client.session_id = headers.get("Mcp-Session-Id")
    client.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
    _, _, is_error, data, _ = client.call_tool("editor_get_state", {"maxListedItems": 0})
    client.send(method="DELETE")
    return None if is_error else (data or {}).get("project")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:18766/mcp", help="the testbed project serves port 18766")
    parser.add_argument("--expect-project", default="AgentMcpTestbed",
                        help="refuse to run unless the editor at --url belongs to this project (the smoke test starts PIE, writes and saves)")
    parser.add_argument("--token", default=None)
    parser.add_argument("--out", default=None)
    parser.add_argument("--skip-slice1", action="store_true")
    parser.add_argument("--skip-pie", action="store_true")
    parser.add_argument("--pie-cycles", type=int, default=1)
    parser.add_argument("--skip-p1", action="store_true")
    parser.add_argument("--skip-p2", action="store_true", help="skip DataTable editing and saving (needs the testbed fixture toolset)")
    parser.add_argument("--skip-p3", action="store_true", help="skip compile, capture, Live Coding, rollback and cancellation (needs the testbed toolset)")
    parser.add_argument("--skip-p4", action="store_true", help="skip Widget Blueprint authoring (needs the testbed toolset)")
    parser.add_argument("--skip-p5", action="store_true", help="skip skills (the project skill checks need the testbed configuration)")
    parser.add_argument("--skip-p6", action="store_true", help="skip creating data assets and importing textures (needs the testbed toolset)")
    args = parser.parse_args()

    # Every editor with the plugin serves the same default port, so make sure the smoke test talks to the intended project.
    try:
        project = connected_project(args.url, args.token)
    except OSError as error:
        print(f"Cannot reach {args.url}: {error}", file=sys.stderr)
        return 2
    if project != args.expect_project:
        print(f"Refusing to run: the editor at {args.url} belongs to project '{project}', expected '{args.expect_project}'.", file=sys.stderr)
        return 2
    print(f"Connected to project '{project}' at {args.url}.", flush=True)

    client = McpClient(args.url, args.token)
    report = Report()
    evidence = {"url": args.url, "startedUtc": datetime.datetime.now(datetime.timezone.utc).isoformat()}

    expected_tools = set(P0_TOOLS)
    if not args.skip_slice1:
        expected_tools |= SLICE1_TOOLS
    if not args.skip_p1:
        expected_tools |= P1_TOOLS
    if not args.skip_p2:
        expected_tools |= P2_TOOLS
    if not args.skip_p3:
        expected_tools |= P3_TOOLS
    if not args.skip_p4:
        expected_tools |= P4_TOOLS
    if not args.skip_p5:
        expected_tools |= P5_TOOLS
    if not args.skip_p6:
        expected_tools |= P6_TOOLS
    try:
        run_p0(client, report, evidence, expected_tools)
        if not args.skip_slice1:
            run_slice1(client, report, evidence, not args.skip_pie, max(1, args.pie_cycles))
        if not args.skip_p1:
            run_p1(client, report, evidence)
        if not args.skip_p2:
            run_p2(client, report, evidence)
        if not args.skip_p3:
            run_p3(client, report, evidence)
        if not args.skip_p4:
            run_p4(client, report, evidence)
        if not args.skip_p5:
            run_p5(client, report, evidence)
        if not args.skip_p6:
            run_p6(client, report, evidence)
        run_protocol_errors(client, report)
    except Exception as error:  # Keep the evidence of the checks that did run.
        report.check("smoke test ran to completion", False, f"{type(error).__name__}: {error}")

    failed = [check for check in report.checks if check["passed"] is False]
    evidence["checks"] = report.checks
    evidence["finishedUtc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    passed = sum(1 for check in report.checks if check["passed"] is True)
    print(f"\n{passed} passed, {len(failed)} failed, {len(report.checks) - passed - len(failed)} info.")

    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            json.dump(evidence, handle, indent=2, ensure_ascii=False)
        print("Evidence written to " + args.out)

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
