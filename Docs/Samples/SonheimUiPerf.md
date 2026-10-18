# Sonheim UI performance — an agent's investigation log

[한국어](SonheimUiPerf.ko.md)

This page records **real work in which an agent used the `perf-investigation` skill and the perf tools to find and fix performance problems in UI nobody had measured**. The project is [Sonheim](https://github.com/chungheonLee0325/Sonheim), an Unreal Engine 5.5 listen-server game.

The stalls of the inventory, monster book and map had been fixed in earlier work. This time the agent was given areas where nobody knew the answer: quests, crafting, chests, loot and damage numbers. The tool results below are excerpts of the real calls of that session (2026-10-17), and the commits are on Sonheim's `feat-ui-perf` branch ([PR #6](https://github.com/chungheonLee0325/Sonheim/pull/6)).

```text
Request → Scenes → run.py baseline → perf_summarize_csv
        → run.py --trace → perf_trace_hotspots → Cause in code → Fix
        → run.py again → perf_compare_csv → Tool improvements
```

## Request

The request set only a goal, on purpose.

> Check the performance of the UI beyond the inventory, monster book and map, and find what is worth improving. Candidates: the quest journal and dialog, crafting, chests, dungeon UI, a burst of notices, the combat HUD. Turn the moments players feel into scenes, and judge them against the frame budget and the run-to-run spread. Fix only what is clearly worth it, then measure again with the same scenes.

It did not say which widget was slow or where to fix it.

## 1. Scenes

The agent read the skill, went through the UI code and added six scenes to the project's scene runner (`UPerfScenarioSubsystem`). It filled the data to late-game size and marked each moment with `CSV_EVENT` and a `PerfUI <moment>` trace scope.

| Scene | Moment measured |
|---|---|
| Loot | Picking up five stacked items at once |
| Kills | Reporting a kill every 0.5 s with all five quests accepted |
| Journal | Selecting another quest in the journal every 0.3 s |
| Hits | Showing twenty damage numbers at once |
| Craft | Opening and closing the crafting bench (11 recipes) with a full bag, then sorting the bag |
| Chest | Opening and closing a chest with 20 filled slots, then sorting the bag |

The dungeon UI was left out of this round because it needs a dungeon run in progress.

## 2. Baseline — `run.py`, `perf_summarize_csv`

With the editor closed, `run.py` measured three runs. The CPU load of other processes averaged 4.8%, 5.8% and 7.6% per run, within the 10% limit.

```text
python run.py --uproject <Sonheim.uproject> --label ui2_before --map /Game/_Maps/GameMap --runs 3
    --game-args "-PerfScenes=HUD,Loot,Kills,Journal,Hits,Craft,Chest -PerfSeconds=10 -PerfWarmup=3 -PerfLabel={label} -PerfRun={run}"
```

The cost of each moment from the `perf_summarize_csv` result (median / max, ms):

| Scene | Moment | Frame | Game Thread |
|---|---|---:|---:|
| HUD (reference) | — | 16.47 (scene average) | 2.49 (scene average) |
| Chest | Open Chest | 30.4 / 32.0 | 16.9 / 19.1 |
| Craft | Open Crafting | 19.4 / 20.3 | 5.6 / 6.3 |
| Loot | Loot | 18.6 / 20.7 | 4.7 / 5.5 |
| Journal | Pick Quest | 17.5 / 19.0 | 3.9 / 5.5 |
| Hits | Hits | 16.8 / 17.8 | 3.8 / 4.7 |
| Kills | Kill | 18.9 / **864.2** | 4.9 / **866.1** |

The agent pointed out two things: opening a chest takes nearly two frames every time, and a kill report is usually light but once stalls for more than 860 ms.

## 3. Finding the cause — `run.py --trace`, `perf_trace_hotspots`

It recorded one trace and called `perf_trace_hotspots` with `bSkipFirst: true`. An excerpt of the result (ms per occurrence of the moment; `caller` is the timer that called it for most of that time):

```text
== Open Chest  (7 times, occurrenceMs 47.0–47.8)
   Client_OpenContainerUI      self 5.29  max 5.61  incl 12.84  <- Blueprint Time
   ConstructObject             self 2.45  max 2.99  incl  6.93  <- InstanceSubobjects
   PostInitProperties          self 1.61  max 1.74  incl  1.61  <- Overlay
   InstanceSubobjects          self 1.10  max 1.24  incl  5.89  <- OverlaySlot

== Kill  (26 times, occurrenceMs … 47.8, 832.5, 15.6 …)
   WBP_NoticeFeed_C …          self 31.03 max 806.56 incl 31.03
   PerfUI Kill                 self 1.02  max 1.56  incl  1.79

== Loot  (8 times)
   Blueprint Time              self 4.76  max 5.16  incl  8.91  <- HandleInventory
   Redraw                      self 1.73  max 1.84  incl  1.89  <- Blueprint Time
   AddToViewport               self 0.56  max 0.57  incl  0.60  <- ExecuteUbergraph_WB_PlayerStatusWidget
```

The same trace called again with the current version of the tool. This version shows a dynamic delegate handler as its own row, such as `HandleInventory (Blueprint Time)` (section 6).

![perf_trace_hotspots result](../Images/perf_trace_hotspots.png)

- **Open Chest:** the cost sits in widget creation (`ConstructObject`, `InstanceSubobjects`) under `Client_OpenContainerUI`. Every opening creates the screen and its slot widgets again.
- **Kill:** a stall the averages did not show appears in `occurrenceMs` (one occurrence of 832.5 ms) and `maxSelfMs` (the notice feed, 806.56 ms). The agent found `Ensure condition failed: CurrentNum == InitialNum` at the same time in the game log and traced it to the notice feed removing rows from its entry list while iterating over that same list (`NoticeWidget.cpp`).
- **Loot:** for every item, `UQuestComponent::HandleInventory` redraws the quest tracker, and an item popup widget is created for each one.

One problem was found outside the trace. The scene runner's log of how many objects are bound to `OnInventoryChanged` grew 3 → 4 → 5 → 6 each time a chest was closed with Tab. The agent confirmed in the code that `Client_CloseContainerUI` could not find the already closed widget, so `RemoveDynamic` never ran.

## 4. Decisions and fixes

| Finding | Decision | Fix |
|---|---|---|
| Opening a chest (GT 16.9 ms) | Over a frame every time → fixed | `bKeepWhenClosed` on the chest screen; the slot grid is rebuilt only when its size changes |
| Chest binding leak | Grows with every open and close → fixed | The chest widget binds the bag when it opens and unbinds it in `NativeDestruct` |
| Notice feed ensure (806 ms) | Stalls development builds, reads a removed entry in shipping builds → fixed | Remove rows while iterating over a copy of the entry list |
| Picking up loot (+2.2 ms) | One frame at the moment of pickup → left as is | — |
| Crafting bench open (+3 ms), quest selection (+1.1 ms), damage numbers (+0.6 ms) | Within budget → left as is | — |

## 5. Re-measuring — `perf_compare_csv`

The same scenes were measured three times as `ui2_after` and compared with `perf_compare_csv`. An excerpt of the result:

```text
E Chest   Open Chest      frame  30.4 ->  22.6   GT  16.9 ->   9.2
E Chest   Close Chest     frame  18.7 ->  17.9   GT   5.2 ->   4.9
  Kills   GameThreadTime  4.20 -> 2.54   change -1.66  noise 0.19  bBeyondNoise
  HUD     GPUTime        15.40 -> 14.75  change -0.65  noise 0.06  bBeyondNoise
```

![perf_compare_csv result](../Images/perf_compare_csv.png)

The agent left the last line out of its results. The GPU time of every scene, including the HUD scene whose code was not touched, dropped by about 0.7 ms, so it did not count differences in scene-average frame time as an effect of the fixes and judged by game thread time and moment cost only. After the fix the bound object count stayed at 2 through repeated opening and closing, and the ensure count was 0.

It also checked the memory cost of keeping the chest screen (+0.94 MB of widget objects, no change in render targets or dynamic materials) and the listen-server client path (the bound count stays at 1; opening costs 9.0–10.8 ms of game thread, the same as on the host).

## 6. What went back into the tools

At the end of its report the agent listed what had been awkward in the tools and the skill, and those points went into Agent MCP.

| Report | Change |
|---|---|
| Dynamic delegate handlers showed mixed together as `Blueprint Time ← HandleInventory`, so the source had to be searched | `perf_trace_hotspots` shows each handler as its own row, `<function> (Blueprint Time)` |
| An ensure's callstack capture set an event's maximum with nothing saying so | `run.py` records the ensure count of each run in `runs.json`, and `perf_summarize_csv` warns |
| A moment that goes through the server (opening a chest on a client) arrives 20–35 frames later, outside the event window | The skill gains the rule "mark it in the client RPC handler" |
| The skill did not name `perf_compare_csv`'s arguments | The skill names `baselineFolder` and `candidateFolder` |

Commit: [Name delegate handlers in hotspots and warn about ensures](https://github.com/chungheonLee0325/AgentMcp/commit/18ffb76).

## About this log

- The agent of this session called the same MCP endpoint (`tools/call`) through a short HTTP client script rather than a connected MCP client. The results are what the editor's perf tools returned.
- The numbers were measured on an RTX 2070 SUPER at 1920×1080 in a development build and are not general performance figures for other environments.
