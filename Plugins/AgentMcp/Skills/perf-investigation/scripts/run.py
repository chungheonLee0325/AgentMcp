"""Plays a project's perf scenes in a standalone game and keeps what the runs leave, for the Agent MCP perf tools to read.

  1. preflight.py: the machine is quiet and no editor of the project is open (stops here otherwise)
  2. a warm-up launch whose numbers are thrown away (shader compiles, file caches), then waits for shader workers to finish
  3. launches of the game (-game, a fixed window, vsync and the frame cap off) until --runs of them ran free of background load:
     every other process's CPU load is sampled each second, and a run whose average exceeds --background is played again
     (at most twice --runs launches)
  4. Saved/Perf/<label>/csv/runs.json lists the invalid runs, which perf_summarize_csv and perf_compare_csv leave out

The game itself chooses what to play and writes the CSV captures; run.py passes it --game-args, where {label} and {run} are
replaced. The scene runner contract is in references/scene-runner.md: captures go to Saved/Perf/<label>/csv/<scene>_r<run>.csv,
and the game quits after its last scene.

--trace records one launch as an Unreal Insights trace (Saved/Perf/<label>/run.utrace) for perf_trace_hotspots, instead of
measuring: named events slow the game, so a trace ranks causes and CSV runs measure.

The game saves the scalability groups (sg.*) it ran with into GameUserSettings.ini when it quits, so --exec "sg.X 2" would carry
into every later launch and into the editor. The file is put back after each launch.

Usage: run.py --uproject <path> --label NAME --game-args "-PerfScenes=HUD -PerfLabel={label} -PerfRun={run}" [--map /Game/Maps/X]
              [--runs 3] [--res 1920x1080] [--exec "r.X 1, sg.Y 2"] [--close name,name] [--accept-busy] [--cpu 10] [--background 10] [--trace]
              [--engine <engine folder>] [--timeout 900]
Needs psutil.
"""
import argparse
import json
import pathlib
import platform
import shlex
import statistics
import subprocess
import sys
import time

import psutil

# Closed for the measurement and started again after it.
RESTORE = {"onedrive", "dropbox"}


def find_editor(uproject, engine):
    """UnrealEditor.exe of --engine, or of the installed engine the project names in EngineAssociation."""
    if engine:
        return pathlib.Path(engine) / "Binaries" / "Win64" / "UnrealEditor.exe"
    association = json.loads(uproject.read_text(encoding="utf-8")).get("EngineAssociation", "")
    if platform.system() == "Windows" and association:
        import winreg
        for root, key in ((winreg.HKEY_LOCAL_MACHINE, rf"SOFTWARE\EpicGames\Unreal Engine\{association}"),):
            try:
                with winreg.OpenKey(root, key) as handle:
                    return pathlib.Path(winreg.QueryValueEx(handle, "InstalledDirectory")[0]) / "Engine" / "Binaries" / "Win64" / "UnrealEditor.exe"
            except OSError:
                pass
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Epic Games\Unreal Engine\Builds") as handle:
                return pathlib.Path(winreg.QueryValueEx(handle, association)[0]) / "Engine" / "Binaries" / "Win64" / "UnrealEditor.exe"
        except OSError:
            pass
    sys.exit("cannot find the engine of the project; pass --engine <engine folder>, e.g. C:/UE_5.5/Engine")


def launch(editor, uproject, args, label, run, log, extra=()):
    width, height = args.res.split("x")
    cvars = "r.VSync 0, t.MaxFPS 0" + "".join(f", {c.strip()}" for c in args.exec.split(",") if c.strip())
    game_args = shlex.split(args.game_args.replace("{label}", label).replace("{run}", str(run)), posix=False)
    command = [str(editor), str(uproject), *([args.map] if args.map else []), "-game", "-windowed", f"-ResX={width}", f"-ResY={height}",
               "-nosplash", "-unattended", f"-ExecCmds={cvars}", *game_args, f"-abslog={log}", *extra]
    return subprocess.Popen(command)


def watch(process, settings, limit):
    """Waits for the game, sampling every other process's CPU load once a second, then puts the user settings file back."""
    saved = settings.read_bytes() if settings.exists() else None
    try:
        game = psutil.Process(process.pid)
        samples = []
        psutil.cpu_percent(None)
        deadline = time.monotonic() + limit
        while process.poll() is None:
            if time.monotonic() > deadline:
                process.kill()
                raise RuntimeError("the game did not finish in time; does its scene runner quit after the last scene?")
            time.sleep(1.0)
            total = psutil.cpu_percent(None)
            try:
                ours = sum(p.cpu_percent(None) for p in [game] + game.children(recursive=True)) / psutil.cpu_count()
            except psutil.Error:
                ours = 0.0
            samples.append(max(0.0, total - ours))
        # The first quarter holds the game's own start-up in the first reading of its children; the steady part is what counts.
        steady = samples[len(samples) // 4:] or samples or [0.0]
        return {"background_avg": round(statistics.mean(steady), 1), "background_max": round(max(steady), 1), "exit": process.returncode}
    finally:
        if saved is not None:
            settings.write_bytes(saved)


def wait_for_shader_workers(limit=600):
    deadline = time.monotonic() + limit
    while any(p.info["name"].lower().startswith("shadercompileworker") for p in psutil.process_iter(["name"])):
        if time.monotonic() > deadline:
            raise RuntimeError("shader compile workers still running")
        time.sleep(5)


def environment(uproject, args, preflight):
    def run(command):
        try:
            return subprocess.run(command, capture_output=True, text=True, timeout=20, shell=True).stdout.strip()
        except Exception:
            return ""
    return {
        "time": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "commit": run(f'git -C "{uproject.parent}" rev-parse --short HEAD'),
        "cpu": run("powershell -c (Get-CimInstance Win32_Processor).Name"),
        "gpu": run('powershell -c "(Get-CimInstance Win32_VideoController).Name -join \\", \\""'),
        "power_plan": run("powercfg /getactivescheme"),
        "resolution": args.res,
        "exec": args.exec,
        "game_args": args.game_args,
        "preflight": preflight,
    }


def restore(preflight):
    started = set()
    for entry in preflight.get("closed", []):
        if entry["name"] in RESTORE and entry.get("exe") and entry["exe"] not in started:
            subprocess.Popen([entry["exe"]], creationflags=getattr(subprocess, "DETACHED_PROCESS", 0))
            started.add(entry["exe"])
    return sorted(started)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--uproject", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--game-args", required=True, help="the scene runner's arguments; {label} and {run} are replaced")
    parser.add_argument("--map", default="")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--res", default="1920x1080")
    parser.add_argument("--exec", default="", help="console variables for every launch, comma separated: 'r.X 1, sg.Y 2'")
    parser.add_argument("--close", default="")
    parser.add_argument("--accept-busy", action="store_true", help="measure with the apps the preflight lists left open (the user's choice)")
    parser.add_argument("--cpu", type=float, default=10.0, help="machine CPU %% the preflight waits to fall under before starting")
    parser.add_argument("--background", type=float, default=10.0, help="average %% of other processes' CPU load that voids a run")
    parser.add_argument("--trace", action="store_true", help="record one launch as an Insights trace instead of measuring")
    parser.add_argument("--engine", default="")
    parser.add_argument("--timeout", type=float, default=900, help="seconds one launch may take")
    args = parser.parse_args()

    # Git Bash rewrites an argument like /Game/Maps/Main into a Windows path before Python sees it.
    if args.map and not args.map.startswith("/"):
        sys.exit(f"--map {args.map} is not a package path such as /Game/Maps/Main; from Git Bash run with MSYS_NO_PATHCONV=1")
    uproject = pathlib.Path(args.uproject).resolve()
    editor = find_editor(uproject, args.engine)
    folder = uproject.parent / "Saved" / "Perf" / args.label
    folder.mkdir(parents=True, exist_ok=True)
    settings = uproject.parent / "Saved" / "Config" / "WindowsEditor" / "GameUserSettings.ini"

    check = subprocess.run([sys.executable, str(pathlib.Path(__file__).with_name("preflight.py")), "--uproject", str(uproject),
                            "--close", args.close, "--cpu", str(args.cpu), "--json", str(folder / "preflight.json"),
                            *(["--accept-busy"] if args.accept_busy else [])], capture_output=True, text=True)
    preflight = json.loads((folder / "preflight.json").read_text(encoding="utf-8"))
    if check.returncode != 0:
        print(check.stdout)
        sys.exit(f"preflight: {preflight['status']}")

    try:
        print("warm-up launch", flush=True)
        watch(launch(editor, uproject, args, "warmup", 0, folder / "game_r0.log"), settings, args.timeout)
        wait_for_shader_workers()
        if args.trace:
            trace = folder / "run.utrace"
            trace.unlink(missing_ok=True)
            info = watch(launch(editor, uproject, args, args.label, 1, folder / "game_trace.log",
                                ("-trace=cpu,frame,bookmark", "-statnamedevents", f"-tracefile={trace}")), settings, args.timeout)
            print(json.dumps({"trace": str(trace), **info}))
            return
        runs = {}
        run = 0
        while sum(not r["invalid"] for r in runs.values()) < args.runs and run < args.runs * 2:
            run += 1
            started = time.time()
            info = watch(launch(editor, uproject, args, args.label, run, folder / f"game_r{run}.log"), settings, args.timeout)
            # A run that wrote no capture measured nothing: a wrong map, scene arguments the game ignores, a crash.
            captures = [p.name for p in (folder / "csv").glob(f"*_r{run}.csv") if p.stat().st_mtime >= started] if (folder / "csv").exists() else []
            if not captures:
                sys.exit(f"run {run} wrote no Saved/Perf/{args.label}/csv/*_r{run}.csv; read {folder / f'game_r{run}.log'} "
                         "(map loaded? scene runner arguments right? did the game crash?)")
            info["captures"] = captures
            info["invalid"] = info["background_avg"] > args.background
            runs[run] = info
            print(f"run {run}: {info}", flush=True)
        invalid = sorted(r for r, i in runs.items() if i["invalid"])
        # Next to the captures, where perf_summarize_csv and perf_compare_csv look for it.
        (folder / "csv").mkdir(exist_ok=True)
        (folder / "csv" / "runs.json").write_text(json.dumps({"invalid": invalid, "runs": runs}, indent=1), encoding="utf-8")
        (folder / "env.json").write_text(json.dumps(environment(uproject, args, preflight), ensure_ascii=False, indent=1), encoding="utf-8")
        valid = len(runs) - len(invalid)
        print(json.dumps({"folder": str(folder), "csv": str(folder / "csv"), "valid_runs": valid, "invalid_runs": invalid}))
        if valid < 2:
            sys.exit("fewer than two runs were free of background load; nothing to compare")
    finally:
        print("restored:", restore(preflight))


if __name__ == "__main__":
    main()
