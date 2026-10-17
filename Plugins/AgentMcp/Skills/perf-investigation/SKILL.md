---
name: perf-investigation
description: Measure, explain and fix performance in an Unreal game - CSV captures of fixed scenes in a standalone game, marked moments for stalls (opening a screen, a list refresh, a drag), Unreal Insights hotspots, ProfileGPU pass breakdowns, before/after comparison that respects run-to-run noise. Use when a screen hitches, a frame budget is exceeded, an optimization needs proof, or someone asks whether something is worth optimizing.
---

# Perf investigation

A performance claim is a before/after pair of measurements whose difference is larger than the noise of both. Everything here
serves that: a quiet machine, fixed scenes, enough runs, marked moments, and a cause found before a fix is written.

```text
Scenes → Baseline runs → Mark moments → Trace → Cause → Fix → Candidate runs → Compare → Check the look
```

## Tools and scripts

| Step | How |
|---|---|
| Quiet machine | `scripts/preflight.py`: closes nothing on its own; lists what must close, waits for builds and git |
| Runs | `scripts/run.py`: warm-up launch, then standalone launches until N runs ran free of background load |
| Averages and stalls | `perf_summarize_csv`: per scene the median of run averages, the spread between runs, and each marked event's cost |
| Before/after | `perf_compare_csv`: change per column and event, flagged only when larger than both spreads |
| Causes of a stall | `run.py --trace`, then `perf_trace_hotspots`: self and inclusive time per timer over each marked moment's frames |
| GPU passes | `ProfileGPU` in the game (a scene runner option), then `perf_read_gpu_profile` on the game log |

The runs happen with the editor closed, because an editor beside the game takes GPU and CPU time from it, so `run.py` refuses to
start while one of the project is open. The perf tools read the files afterwards, with the editor open again.

The game needs a scene runner: a development-only subsystem that plays fixed scenes from its command line, writes one CSV capture
per scene and quits. `skills_get` with `file: references/scene-runner.md` gives the contract and a skeleton.

## Steps

1. **Scenes.** Write or extend the project's scene runner. Measure the moments players feel, not only idle screens: opening and
   closing a screen, a list refreshing after a data change, a drag and drop, a burst of notifications, with data at its late-game
   size. Mark each moment with `CSV_EVENT` and a `TRACE_CPUPROFILER_EVENT_SCOPE_TEXT` named `"PerfUI <moment>"`.
2. **Baseline.** Ask the user before `--close` names anything of theirs; if they would rather keep an app open, `--accept-busy`
   measures anyway and lists it in `preflight.json`, and background-load sampling still voids the runs it disturbs. Then:
   ```text
   python scripts/run.py --uproject <path> --label before --map /Game/Maps/Main --runs 3 ^
       --game-args "-PerfScenes=HUD,OpenClose -PerfSeconds=10 -PerfWarmup=3 -PerfLabel={label} -PerfRun={run}"
   ```
   From Git Bash, prefix the command with `MSYS_NO_PATHCONV=1`, or it rewrites `/Game/...` into a Windows path.
   `perf_summarize_csv` with `folder: Saved/Perf/before/csv`. Averages tell steady cost; event medians and maxima tell stalls.
3. **Is it worth it?** Compare the cost with the frame budget and with the noise. A cost below the spread between runs is not
   worth optimizing, and saying so with the numbers is a result.
4. **Cause.** `run.py --trace --label trace ...`, then `perf_trace_hotspots` with `traceFile: Saved/Perf/trace/run.utrace`.
   Read self time first, then each timer's `caller`. A UFUNCTION run through a dynamic delegate is its own row,
   `<function> (Blueprint Time)`, whose self time is that function's body. `occurrenceMs` and `maxSelfMs` show a cold first occurrence that the averages hide; `bSkipFirst` leaves it out.
5. **Fix** the cause the trace names, at the narrowest place that removes it.
6. **Candidate.** The same `run.py` command with `--label after`, then `perf_compare_csv` with
   `baselineFolder: Saved/Perf/before/csv` and `candidateFolder: Saved/Perf/after/csv`. Report only
   changes flagged beyond noise; for changes under 0.5 ms use 6 runs.
7. **Look and behavior.** A faster frame that looks or behaves differently is not done: compare screenshots (`-PerfShots`), and
   check the changed flow in game, including reopening a screen and a client of a listen server when the code has a client path.

## Rules learned the hard way

- **Read rows against the header at the end.** The CSV profiler adds a column when a stat first appears; parsers that use the
  first header row read a fraction of the frames. The perf tools handle it; a hand-written parser must too.
- **Settings leak between runs.** `sg.*` set from the console is saved into `GameUserSettings.ini` when the game quits. `run.py`
  restores the file after each launch; anything else that launches the game must do the same, and a control run with a
  do-nothing variable catches a leak.
- **One variable at a time**, each against the same baseline, then the chosen combination measured together.
- **Warm and cold differ.** The first opening of a screen loads assets and creates widgets; later ones may not. Measure and report
  both.
- **Mark where the work happens.** A moment that goes through the server (opening a chest on a client) does its UI work when
  the reply arrives, 20-35 frames after the request; mark it in the client RPC handler, not where the input was sent, or no
  event window catches it.
- **Two processes share one GPU.** In a listen server plus client test on one machine, frame time is meaningless; compare game
  thread time.
- **Pooling has a memory side.** When widgets are kept for reuse, compare `obj list` after `obj gc` with and without keeping;
  a kept screen must release what each opening creates (brushes on render targets, dynamic materials), including pooled entries.
- **Ensures stall development builds.** Each ensure records a callstack (hundreds of milliseconds); `run.py` counts them per run
  and `perf_summarize_csv` warns. Fix the ensure, or judge by medians, before blaming the moment.
- **Traces rank, CSV runs measure.** Named events (`-statnamedevents`) and `ProfileGPU` add overhead: use their numbers to find
  causes, and CSV runs to state results.
