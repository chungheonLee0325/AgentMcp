# Scene runner contract

`run.py` launches the game and samples the machine; the game decides what to play. A scene runner is a small development-only
game instance subsystem that reads its own command-line arguments, plays fixed scenes, records one CSV capture per scene and
quits. Every project writes its own, since only the project knows how to open its inventory or start its boss fight.

## What run.py and the perf tools expect

| Item | Contract |
|---|---|
| Activation | Only when the scene argument is present (for example `-PerfScenes=HUD,Inventory`); a normal launch is untouched |
| Label and run | `-PerfLabel=<label> -PerfRun=<n>`, passed through `--game-args "... -PerfLabel={label} -PerfRun={run}"` |
| Captures | `Saved/Perf/<label>/csv/<scene>_r<n>.csv`, one per scene and run, through `FCsvProfiler::BeginCapture` / `EndCapture` |
| Settling | Wait for the player, then let each scene settle for a few seconds before its capture starts |
| Same view | Every scene starts from the same place and camera, so the world behind the UI draws the same in every run |
| Marked moments | `CSV_EVENT(PerfUI, TEXT("%s"), *Name)` for perf_summarize_csv, and `TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*(TEXT("PerfUI ") + Name))` around the call for perf_trace_hotspots |
| End | `FPlatformMisc::RequestExit(false)` after the last capture has been written |
| Build | Development builds only: `#if !UE_BUILD_SHIPPING && CSV_PROFILER` |

Scenes worth having: the idle HUD, each heavy screen open, and the moments a player feels: opening and closing a screen, a list
refreshing after a data change, a drag and drop, a burst of notifications. Fill data to its late-game size (a full inventory,
every species found) so the scene measures the case that hurts. A drag can be played with synthetic pointer events through
`FSlateApplication::ProcessMouseButtonDownEvent`, `ProcessMouseMoveEvent` and `ProcessMouseButtonUpEvent`.

## Skeleton

The phases matter more than the scenes: `EndCapture` finishes on the next frame and writes the file on another thread, so waiting
for its future in the frame that ended the capture holds the very frame it needs. The runner polls it on later frames instead.

```cpp
UCLASS()
class UPerfScenarioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override { FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); Super::Deinitialize(); }

private:
	enum class EPhase : uint8 { WaitingForPlayer, Settling, Recording, Writing, Done };
	bool Tick(float DeltaSeconds);
	void SetUp(const FString& Scene);           // put the player in place, open what the scene measures
	void Drive(const FString& Scene, float Time); // what the scene does every frame: push notices, swap items, drag
	void TearDown();                            // close everything the scene opened

	TArray<FString> Scenes;
	FString Label;
	int32 Run = 1;
	float WarmupSeconds = 3.f;
	float RecordSeconds = 10.f;
	int32 SceneIndex = 0;
	EPhase Phase = EPhase::WaitingForPlayer;
	float PhaseTime = 0.f;
	FTSTicker::FDelegateHandle TickHandle;
	TSharedFuture<FString> Written;
};
```

```cpp
void UPerfScenarioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING && CSV_PROFILER
	FString List;
	if (!FParse::Value(FCommandLine::Get(), TEXT("PerfScenes="), List, false)) return;
	List.ParseIntoArray(Scenes, TEXT(","));
	FParse::Value(FCommandLine::Get(), TEXT("PerfLabel="), Label);
	FParse::Value(FCommandLine::Get(), TEXT("PerfRun="), Run);
	FParse::Value(FCommandLine::Get(), TEXT("PerfWarmup="), WarmupSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("PerfSeconds="), RecordSeconds);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UPerfScenarioSubsystem::Tick));
#endif
}

bool UPerfScenarioSubsystem::Tick(const float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING && CSV_PROFILER
	PhaseTime += DeltaSeconds;
	const FString Scene = Scenes.IsValidIndex(SceneIndex) ? Scenes[SceneIndex].TrimStartAndEnd() : FString();
	switch (Phase)
	{
	case EPhase::WaitingForPlayer:
		if (PhaseTime < 5.f || !IsPlayerReady()) return true;   // the project's own test: pawn possessed, HUD up
		SetUp(Scene);
		Phase = EPhase::Settling;
		PhaseTime = 0.f;
		return true;
	case EPhase::Settling:
		Drive(Scene, PhaseTime);
		if (PhaseTime < WarmupSeconds) return true;
		FCsvProfiler::Get()->BeginCapture(-1, FPaths::ProjectSavedDir() / TEXT("Perf") / Label / TEXT("csv"),
		                                  FString::Printf(TEXT("%s_r%d.csv"), *Scene, Run));
		Phase = EPhase::Recording;
		PhaseTime = 0.f;
		return true;
	case EPhase::Recording:
		Drive(Scene, WarmupSeconds + PhaseTime);
		if (PhaseTime < RecordSeconds) return true;
		Written = FCsvProfiler::Get()->EndCapture();
		TearDown();
		Phase = EPhase::Writing;
		return true;
	case EPhase::Writing:
		if (!Written.IsReady()) return true;
		if (++SceneIndex < Scenes.Num())
		{
			SetUp(Scenes[SceneIndex].TrimStartAndEnd());
			Phase = EPhase::Settling;
			PhaseTime = 0.f;
			return true;
		}
		Phase = EPhase::Done;
		FPlatformMisc::RequestExit(false);
		return false;
	default:
		return false;
	}
#else
	return false;
#endif
}
```

A marked moment inside `Drive`:

```cpp
CSV_DEFINE_CATEGORY(PerfUI, true);

const FString Event = FString::Printf(TEXT("Open %s"), *Screen.ToString());
CSV_EVENT(PerfUI, TEXT("%s"), *Event);
TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*(TEXT("PerfUI ") + Event));
OpenScreen(Screen);
```

Optional arguments that proved useful: `-PerfShots` (a screenshot as each capture starts, for comparing looks), `-PerfProfileGPU=<scene>`
(`ProfileGPU` as that scene's capture starts, read with perf_read_gpu_profile from the game log) and `-PerfEndCmds="obj gc;obj list class=Widget"`
(console commands after the last scene, to see what stays in memory).
