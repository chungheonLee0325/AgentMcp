"""Checks that the machine is quiet enough to measure a game, before run.py launches it.

Each running process that loads the CPU or the GPU falls in one class:
  project   an editor or game of the project being measured: it takes GPU and CPU time from the run, so it must be closed first
  wait      builds, shader compiles and git (another agent's or a tool's): never killed, waited for
  approval  other editors, games, browsers, sync and chat clients while they are busy, and any unknown process using more than a
            quarter of a core: closed only when named in --close
  note      remote desktops, antivirus, and approval-class apps that sit idle: never closed, written into the report
Then the whole machine's CPU is sampled; measuring starts only once it stays under --cpu.

Usage: preflight.py --uproject <path> [--close name,name] [--accept-busy] [--wait-seconds 300] [--cpu 10] [--json out.json]
Exit codes: 0 quiet, 2 processes need approval or the project's editor is open (listed), 3 still busy after waiting.
Needs psutil.
"""
import argparse
import json
import pathlib
import sys
import time

import psutil

# Process names (lower case, no extension) by class.
WAIT = {"unrealbuildtool", "shadercompileworker", "dotnet", "msbuild", "cl", "link", "git", "unrealpak", "unrealeditor-cmd"}
APPROVAL = {"unrealeditor", "chrome", "msedge", "firefox", "onedrive", "dropbox", "steam", "steamwebhelper", "discord", "slack",
            "obs64", "epicgameslauncher"}
# Never closed, only written into the report: a remote desktop may be how the user is at this machine (and it encodes on the GPU).
NOTE = {"parsecd", "mstsc", "anydesk", "teamviewer", "msmpeng"}
# Part of Windows: never a candidate to close, even when busy.
SYSTEM = {"system", "system idle process", "registry", "dwm", "csrss", "audiodg", "svchost", "wmiprvse", "searchindexer", "python"}
# Any other process above this share of one core (a game whose name is unknown, a scan, a render) needs approval too.
BUSY_FLOOR = 25.0
GAME_SUFFIXES = ("-win64-shipping", "-win64-development", "-win64-test")
# An approval-class app under this share of one core is idle and only noted; editors and games always count.
LOAD_FLOOR = 2.0


def name_of(process):
    return process.info["name"].lower().removesuffix(".exe")


def classify(uproject):
    """Running processes worth a look, by class, with their CPU share over three seconds."""
    procs = list(psutil.process_iter(["pid", "name", "cmdline"]))
    for process in procs:
        try:
            process.cpu_percent(None)
        except psutil.Error:
            pass
    # Three seconds, so an idle app's momentary blip (a chat client syncing) does not count as load.
    time.sleep(3.0)
    found = {"project": [], "wait": [], "approval": [], "note": []}
    for process in procs:
        try:
            name = name_of(process)
            load = process.cpu_percent(None)
            command = " ".join(process.info.get("cmdline") or [])
        except psutil.Error:
            continue
        entry = {"pid": process.info["pid"], "name": name, "cpu": round(load, 1)}
        try:
            entry["exe"] = process.exe()
        except psutil.Error:
            pass
        # Only an editor or a game counts: shells and scripts that merely name the project (this one included) do not.
        if (name.startswith("unrealeditor") or name.endswith(GAME_SUFFIXES)) and uproject.lower() in command.lower().replace("/", "\\"):
            found["project"].append(entry)
        elif name in WAIT:
            found["wait"].append(entry)
        elif name in NOTE:
            found["note"].append(entry)
        elif name in APPROVAL or name.endswith(GAME_SUFFIXES):
            if load < LOAD_FLOOR and name != "unrealeditor" and not name.endswith(GAME_SUFFIXES):
                found["note"].append(entry)
            else:
                found["approval"].append(entry)
        elif load >= BUSY_FLOOR and name not in SYSTEM:
            found["approval"].append(entry)
    return found


def close(entries):
    for entry in entries:
        try:
            process = psutil.Process(entry["pid"])
            process.terminate()
            process.wait(15)
        except psutil.TimeoutExpired:
            process.kill()
        except psutil.Error:
            pass


def finish(report, out, code):
    text = json.dumps(report, ensure_ascii=False, indent=1)
    print(text)
    if out:
        pathlib.Path(out).write_text(text, encoding="utf-8")
    sys.exit(code)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--uproject", required=True)
    parser.add_argument("--close", default="", help="approval-class names to close, comma separated")
    parser.add_argument("--wait-seconds", type=int, default=300)
    parser.add_argument("--cpu", type=float, default=10.0)
    parser.add_argument("--accept-busy", action="store_true",
                        help="the user chose to measure with these apps open: list them in the report instead of stopping")
    parser.add_argument("--json")
    args = parser.parse_args()
    uproject = str(pathlib.Path(args.uproject).resolve())
    approved = {n.strip().lower() for n in args.close.split(",") if n.strip()}
    report = {"started": time.strftime("%Y-%m-%dT%H:%M:%S"), "closed": [], "waited": [], "cpu": None, "status": None}

    found = classify(uproject)
    report["noted"] = sorted({e["name"] for e in found["note"]})
    if found["project"]:
        # An editor can hold unsaved work, so it is the user's (or an agent's, through the editor) to close, never killed here.
        report["status"] = "project-open"
        report["pending"] = found["project"]
        finish(report, args.json, 2)
    to_close = [e for e in found["approval"] if e["name"] in approved]
    close(to_close)
    report["closed"] = to_close
    pending = [e for e in found["approval"] if e["name"] not in approved]
    if pending and args.accept_busy:
        report["accepted_busy"] = pending
        pending = []
    if pending:
        report["status"] = "needs-approval"
        report["pending"] = pending
        finish(report, args.json, 2)

    deadline = time.monotonic() + args.wait_seconds
    while True:
        found = classify(uproject)
        busy = found["wait"] + found["project"]
        cpu = psutil.cpu_percent(interval=5)
        report["cpu"] = cpu
        if not busy and cpu < args.cpu:
            report["status"] = "quiet"
            finish(report, args.json, 0)
        if busy:
            report["waited"] = sorted({e["name"] for e in busy})
        if time.monotonic() > deadline:
            report["status"] = "busy"
            report["busy"] = busy
            finish(report, args.json, 3)
        time.sleep(5)


if __name__ == "__main__":
    main()
