#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"

#include "AgentMcpPerfTools.generated.h"

class UAgentMcpAsyncResult;

USTRUCT(BlueprintType)
struct FAgentMcpPerfStat
{
	GENERATED_BODY()

	/** CSV profiler column, such as FrameTime or Exclusive/GameThread/UI. */
	UPROPERTY()
	FString Column;

	/** Median of the runs' averages. Milliseconds for timings. */
	UPROPERTY()
	double Median = 0.0;

	/** Largest run average less the smallest: the noise between runs. */
	UPROPERTY()
	double Spread = 0.0;

	/** Median of the runs' 95th percentiles. */
	UPROPERTY()
	double P95 = 0.0;

	/** Runs that had the column. */
	UPROPERTY()
	int32 Runs = 0;
};

USTRUCT(BlueprintType)
struct FAgentMcpPerfEvent
{
	GENERATED_BODY()

	/** Event name without its category, such as "Open Menu" for CSV_EVENT(PerfUI, "Open Menu"). */
	UPROPERTY()
	FString Name;

	/** Times the event happened, over every run. */
	UPROPERTY()
	int32 Count = 0;

	/** The worst FrameTime of the event's frame and the frames after it (eventWindowFrames), median and maximum over the events. */
	UPROPERTY()
	double FrameMedian = 0.0;

	UPROPERTY()
	double FrameMax = 0.0;

	/** The same for GameThreadTime. */
	UPROPERTY()
	double GameThreadMedian = 0.0;

	UPROPERTY()
	double GameThreadMax = 0.0;
};

USTRUCT(BlueprintType)
struct FAgentMcpPerfGroup
{
	GENERATED_BODY()

	/** Files named <name>_r<run>.csv form one group; any other file is a group of its own. */
	UPROPERTY()
	FString Name;

	UPROPERTY()
	TArray<FString> Files;

	/** Frames read over every run. */
	UPROPERTY()
	int32 Frames = 0;

	UPROPERTY()
	TArray<FAgentMcpPerfStat> Stats;

	UPROPERTY()
	TArray<FAgentMcpPerfEvent> Events;
};

USTRUCT(BlueprintType)
struct FAgentMcpPerfSummaryResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Folder;

	UPROPERTY()
	TArray<FAgentMcpPerfGroup> Groups;

	/** Files left out because runs.json in the folder marks their run invalid. */
	UPROPERTY()
	TArray<FString> SkippedFiles;

	/** Columns asked for that no file had, and files that could not be read. */
	UPROPERTY()
	TArray<FString> Warnings;
};

USTRUCT(BlueprintType)
struct FAgentMcpPerfStatChange
{
	GENERATED_BODY()

	UPROPERTY()
	FString Group;

	UPROPERTY()
	FString Column;

	UPROPERTY()
	double Baseline = 0.0;

	UPROPERTY()
	double Candidate = 0.0;

	/** Candidate less baseline. */
	UPROPERTY()
	double Change = 0.0;

	/** The larger of the two spreads. */
	UPROPERTY()
	double Noise = 0.0;

	/** The change is larger than the noise of both measurements; anything else is not a result. */
	UPROPERTY()
	bool bBeyondNoise = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpPerfEventChange
{
	GENERATED_BODY()

	UPROPERTY()
	FString Group;

	UPROPERTY()
	FString Event;

	UPROPERTY()
	double BaselineFrame = 0.0;

	UPROPERTY()
	double CandidateFrame = 0.0;

	UPROPERTY()
	double FrameChange = 0.0;

	UPROPERTY()
	double BaselineGameThread = 0.0;

	UPROPERTY()
	double CandidateGameThread = 0.0;

	UPROPERTY()
	double GameThreadChange = 0.0;
};

USTRUCT(BlueprintType)
struct FAgentMcpPerfCompareResult
{
	GENERATED_BODY()

	UPROPERTY()
	FAgentMcpPerfSummaryResult Baseline;

	UPROPERTY()
	FAgentMcpPerfSummaryResult Candidate;

	/** Every column of every group found in both folders. */
	UPROPERTY()
	TArray<FAgentMcpPerfStatChange> Stats;

	/** Event medians of events found in both. */
	UPROPERTY()
	TArray<FAgentMcpPerfEventChange> Events;
};

USTRUCT(BlueprintType)
struct FAgentMcpGpuPass
{
	GENERATED_BODY()

	/** 0 for the frame, 1 for its children and so on. */
	UPROPERTY()
	int32 Depth = 0;

	UPROPERTY()
	double Ms = 0.0;

	/** Pass name with its draw and dispatch counts, as ProfileGPU wrote it. */
	UPROPERTY()
	FString Name;
};

USTRUCT(BlueprintType)
struct FAgentMcpGpuProfileResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString LogFile;

	/** ProfileGPU dumps found in the log; the one returned is chosen by occurrence. */
	UPROPERTY()
	int32 Profiles = 0;

	/** "total GPU time" of the dump: every view and frame it recorded, profiling overhead included. */
	UPROPERTY()
	double TotalMs = 0.0;

	/** Passes at or above minMs and at most maxDepth deep, in the order of the dump. */
	UPROPERTY()
	TArray<FAgentMcpGpuPass> Passes;
};

USTRUCT(BlueprintType)
struct FAgentMcpPerfTimer
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** Milliseconds per occurrence of the marker, less what the timer called that has a timer of its own. */
	UPROPERTY()
	double SelfMs = 0.0;

	/** Milliseconds per occurrence, everything it called included. */
	UPROPERTY()
	double InclusiveMs = 0.0;
};

USTRUCT(BlueprintType)
struct FAgentMcpPerfMarker
{
	GENERATED_BODY()

	/** Marker scope name without the prefix, such as "Open Menu" for the scope "PerfUI Open Menu". */
	UPROPERTY()
	FString Name;

	UPROPERTY()
	int32 Count = 0;

	/** Largest self time first. */
	UPROPERTY()
	TArray<FAgentMcpPerfTimer> Timers;
};

USTRUCT(BlueprintType)
struct FAgentMcpPerfHotspotsResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString TraceFile;

	/** The timing events exported from the trace, next to it. */
	UPROPERTY()
	FString ExportFile;

	/** UnrealInsights.exe exported the trace in this call; false when an export at least as new as the trace was reused. */
	UPROPERTY()
	bool bExported = false;

	UPROPERTY()
	TArray<FAgentMcpPerfMarker> Markers;
};

/**
 * Reads what a profiled run of the game left behind: CSV profiles, ProfileGPU dumps in a log and Unreal Insights traces.
 * The runs themselves are played outside the editor (the perf-investigation skill's scripts), since an editor running beside the
 * game takes GPU and CPU time from it. Files must be inside the project folder.
 */
UCLASS(meta = (McpToolset = "perf"))
class UAgentMcpPerfTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Summarizes CSV profiler captures: per group of runs, each column's median of the run averages, the spread between runs and the
	 * 95th percentile, and how many milliseconds each marked event (CSV_EVENT) cost in its frame and the frames after it.
	 * Rows are read against the header repeated at the end of the file, since the profiler adds a column whenever a stat first appears.
	 * A runs.json in the folder ({"invalid": [2, 5]}) leaves out the files of those runs (<name>_r2.csv, <name>_r5.csv).
	 * @param Folder Folder of .csv files, relative to the project folder or absolute inside it.
	 * @param Pattern File name wildcard.
	 * @param Columns Comma-separated columns; empty reads FrameTime, GameThreadTime, RenderThreadTime, RHIThreadTime and GPUTime.
	 * @param EventWindowFrames Frames from an event's own frame on that count as its cost (1-30).
	 * @return Groups of runs with their stats and events.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Perf", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpPerfSummaryResult SummarizeCsv(const FString& Folder, const FString& Pattern = TEXT("*.csv"), const FString& Columns = TEXT(""), int32 EventWindowFrames = 3);

	/**
	 * Compares two folders of CSV captures summarized as perf_summarize_csv does: per group and column the change of the median, and
	 * whether it is larger than the spread of both measurements; per marked event the change of its medians.
	 * @param BaselineFolder Folder of the runs before the change.
	 * @param CandidateFolder Folder of the runs after it.
	 * @param Pattern File name wildcard for both folders.
	 * @param Columns Comma-separated columns; empty reads the default five.
	 * @param EventWindowFrames Frames from an event's own frame on that count as its cost (1-30).
	 * @return Both summaries and the changes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Perf", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpPerfCompareResult CompareCsv(const FString& BaselineFolder, const FString& CandidateFolder, const FString& Pattern = TEXT("*.csv"), const FString& Columns = TEXT(""), int32 EventWindowFrames = 3);

	/**
	 * Reads a ProfileGPU dump ("Perf marker hierarchy") from a log file as a tree of passes. Run the game with -ExecCmds or a console
	 * command ProfileGPU while it shows what to measure; the dump goes to its log. Times include the profiler's overhead, so they rank
	 * passes; measure changes with CSV captures.
	 * @param LogFile Log file, relative to the project folder or absolute inside it.
	 * @param MinMs Leaves out passes faster than this.
	 * @param MaxDepth Leaves out passes deeper than this (0 is the frame).
	 * @param Occurrence Which dump of the log: 0 the first, -1 the last.
	 * @return Total GPU time and the passes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Perf", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpGpuProfileResult ReadGpuProfile(const FString& LogFile, double MinMs = 0.1, int32 MaxDepth = 4, int32 Occurrence = -1);

	/**
	 * Finds where the frames of marked moments go in an Unreal Insights trace: exports the thread's timing events with
	 * UnrealInsights.exe (no window) unless an export at least as new as the trace is next to it, then for every scope whose name
	 * starts with markerPrefix adds up each timer's self and inclusive time over the marker's frame and the frames after it.
	 * Record the trace with -trace=cpu,frame -statnamedevents -tracefile=<file>, and mark moments in game code with
	 * TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*(TEXT("PerfUI ") + Name)). Named events cost time: the numbers rank causes.
	 * @param TraceFile .utrace file, relative to the project folder or absolute inside it.
	 * @param MarkerPrefix Scope name prefix that marks a moment.
	 * @param Thread Thread whose timers are read.
	 * @param FrameTimer Depth-0 timer that spans one frame on that thread.
	 * @param WindowFrames Frames from the marker's own frame on that are added up (1-30).
	 * @param Top Timers kept per marker (1-200).
	 * @param bSkipFirst Leaves out each marker's first time, which pays for loading and first creation.
	 * @param TimeoutSeconds How long exporting and reading may take.
	 * @return Markers with their timers, largest self time first.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Perf", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static UAgentMcpAsyncResult* TraceHotspots(const FString& TraceFile, const FString& MarkerPrefix = TEXT("PerfUI "), const FString& Thread = TEXT("GameThread"),
	                                           const FString& FrameTimer = TEXT("FEngineLoop::Tick"), int32 WindowFrames = 3, int32 Top = 20, bool bSkipFirst = false,
	                                           float TimeoutSeconds = 900.0f);
};
