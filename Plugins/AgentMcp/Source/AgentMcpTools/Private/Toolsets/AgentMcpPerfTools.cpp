#include "AgentMcpPerfTools.h"

#include "AgentMcpAsyncResult.h"
#include "Algo/BinarySearch.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UE::AgentMcp::PerfToolsPrivate
{
	const TCHAR* DefaultColumns = TEXT("FrameTime,GameThreadTime,RenderThreadTime,RHIThreadTime,GPUTime");

	/** A file or folder inside the project folder, from a path relative to it or absolute. Raises INVALID_ARGUMENT or NOT_FOUND. */
	bool ResolveProjectPath(const FString& Path, const TCHAR* ArgumentName, bool bDirectory, FString& OutFullPath)
	{
		const FString Trimmed = Path.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'%s' is empty."), ArgumentName));
			return false;
		}
		const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FString Full = FPaths::IsRelative(Trimmed) ? FPaths::Combine(ProjectDir, Trimmed) : Trimmed;
		Full = FPaths::ConvertRelativePathToFull(Full);
		FPaths::NormalizeFilename(Full);
		FPaths::CollapseRelativeDirectories(Full);
		if (!FPaths::IsUnderDirectory(Full, ProjectDir))
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'%s' (%s) is outside the project folder."), ArgumentName, *Full),
			               FString::Printf(TEXT("Pass a path inside %s, such as Saved/Perf/<label>."), *ProjectDir));
			return false;
		}
		const bool bExists = bDirectory ? IFileManager::Get().DirectoryExists(*Full) : IFileManager::Get().FileExists(*Full);
		if (!bExists)
		{
			RaiseToolError(TEXT("NOT_FOUND"), FString::Printf(TEXT("'%s' (%s) does not exist."), ArgumentName, *Full));
			return false;
		}
		OutFullPath = Full;
		return true;
	}

	TArray<FString> ParseColumns(const FString& Columns)
	{
		TArray<FString> Out;
		(Columns.TrimStartAndEnd().IsEmpty() ? FString(DefaultColumns) : Columns).ParseIntoArray(Out, TEXT(","), true);
		for (FString& Column : Out)
		{
			Column.TrimStartAndEndInline();
		}
		Out.RemoveAll([](const FString& Column) { return Column.IsEmpty(); });
		return Out;
	}

	double Median(TArray<double> Values)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}
		Values.Sort();
		const int32 Mid = Values.Num() / 2;
		return Values.Num() % 2 ? Values[Mid] : (Values[Mid - 1] + Values[Mid]) * 0.5;
	}

	struct FColumnStats
	{
		double Average = 0.0;
		double P95 = 0.0;
	};

	struct FEventHit
	{
		double Frame = 0.0;
		double GameThread = 0.0;
	};

	struct FCsvRun
	{
		TMap<FString, FColumnStats> Columns;
		TMap<FString, TArray<FEventHit>> Events;
		int32 Frames = 0;
	};

	/**
	 * One capture. The profiler adds a column whenever a stat first appears, so rows grow during the capture and the full header is
	 * the one repeated at the end, ahead of the metadata row ([HasHeaderRowAtEnd] and the like). Columns only go on at the end, so a
	 * column's index in that header holds for every row long enough to have it. The first column holds events, mostly empty.
	 */
	bool ReadCsvRun(const FString& File, const TArray<FString>& Columns, int32 EventWindow, FCsvRun& Out)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *File))
		{
			return false;
		}
		TArray<TArray<FString>> Rows;
		Rows.Reserve(Lines.Num());
		const TArray<FString>* Header = nullptr;
		for (const FString& Line : Lines)
		{
			TArray<FString>& Row = Rows.AddDefaulted_GetRef();
			Line.ParseIntoArray(Row, TEXT(","), /*bCullEmpty=*/false);
		}
		for (const TArray<FString>& Row : Rows)
		{
			if (Row.Num() && Row[0] == TEXT("EVENTS"))
			{
				Header = &Row;
			}
		}
		if (!Header)
		{
			return false;
		}
		TArray<const TArray<FString>*> Frames;
		for (const TArray<FString>& Row : Rows)
		{
			if (Row.Num() > 1 && Row[0] != TEXT("EVENTS") && !Row[0].StartsWith(TEXT("[")))
			{
				Frames.Add(&Row);
			}
		}
		Out.Frames = Frames.Num();
		for (const FString& Column : Columns)
		{
			const int32 Index = Header->IndexOfByKey(Column);
			if (Index == INDEX_NONE)
			{
				continue;
			}
			TArray<double> Values;
			for (const TArray<FString>* Row : Frames)
			{
				if (Index < Row->Num() && !(*Row)[Index].IsEmpty())
				{
					Values.Add(FCString::Atod(*(*Row)[Index]));
				}
			}
			if (Values.IsEmpty())
			{
				continue;
			}
			Values.Sort();
			double Sum = 0.0;
			for (const double Value : Values)
			{
				Sum += Value;
			}
			FColumnStats& Stats = Out.Columns.Add(Column);
			Stats.Average = Sum / Values.Num();
			Stats.P95 = Values[FMath::Min(Values.Num() - 1, static_cast<int32>(Values.Num() * 0.95))];
		}
		const int32 FrameIndex = Header->IndexOfByKey(TEXT("FrameTime"));
		const int32 GameIndex = Header->IndexOfByKey(TEXT("GameThreadTime"));
		auto ValueAt = [](const TArray<FString>& Row, const int32 Index) { return Index != INDEX_NONE && Index < Row.Num() ? FCString::Atod(*Row[Index]) : 0.0; };
		for (int32 i = 0; i < Frames.Num(); ++i)
		{
			const FString& EventText = (*Frames[i])[0];
			if (EventText.IsEmpty())
			{
				continue;
			}
			TArray<FString> Names;
			EventText.ParseIntoArray(Names, TEXT(";"), true);
			for (const FString& Event : Names)
			{
				FString Name = Event;
				int32 Slash = INDEX_NONE;
				if (Name.FindLastChar(TEXT('/'), Slash))
				{
					Name.RightChopInline(Slash + 1);
				}
				Name.TrimStartAndEndInline();
				FEventHit Hit;
				for (int32 j = i; j < FMath::Min(Frames.Num(), i + EventWindow); ++j)
				{
					Hit.Frame = FMath::Max(Hit.Frame, ValueAt(*Frames[j], FrameIndex));
					Hit.GameThread = FMath::Max(Hit.GameThread, ValueAt(*Frames[j], GameIndex));
				}
				Out.Events.FindOrAdd(Name).Add(Hit);
			}
		}
		return true;
	}

	/** Run numbers runs.json marks invalid, as the perf-investigation scripts write it: {"invalid": [2, 5]}. */
	TSet<int32> ReadInvalidRuns(const FString& Folder)
	{
		TSet<int32> Invalid;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(Folder / TEXT("runs.json"))))
		{
			return Invalid;
		}
		TSharedPtr<FJsonObject> Json;
		const TArray<TSharedPtr<FJsonValue>>* Runs = nullptr;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid() && Json->TryGetArrayField(TEXT("invalid"), Runs))
		{
			for (const TSharedPtr<FJsonValue>& Run : *Runs)
			{
				Invalid.Add(static_cast<int32>(Run->AsNumber()));
			}
		}
		return Invalid;
	}

	/** "Inventory_r3.csv" is run 3 of group "Inventory"; a file without _r<n> is its own group, run 0. */
	void SplitRunName(const FString& File, FString& OutGroup, int32& OutRun)
	{
		const FString Stem = FPaths::GetBaseFilename(File);
		FRegexMatcher Matcher(FRegexPattern(TEXT("^(.*)_r(\\d+)$")), Stem);
		if (Matcher.FindNext())
		{
			OutGroup = Matcher.GetCaptureGroup(1);
			OutRun = FCString::Atoi(*Matcher.GetCaptureGroup(2));
			return;
		}
		OutGroup = Stem;
		OutRun = 0;
	}

	FAgentMcpPerfSummaryResult Summarize(const FString& Folder, const FString& Pattern, const TArray<FString>& Columns, int32 EventWindow)
	{
		FAgentMcpPerfSummaryResult Result;
		Result.Folder = Folder;
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Folder / (Pattern.IsEmpty() ? FString(TEXT("*.csv")) : Pattern)), true, false);
		Files.Sort();
		const TSet<int32> Invalid = ReadInvalidRuns(Folder);
		TMap<FString, TArray<TPair<FString, FCsvRun>>> Groups;
		TArray<FString> GroupOrder;
		for (const FString& File : Files)
		{
			FString Group;
			int32 Run = 0;
			SplitRunName(File, Group, Run);
			if (Run > 0 && Invalid.Contains(Run))
			{
				Result.SkippedFiles.Add(File);
				continue;
			}
			FCsvRun Read;
			if (!ReadCsvRun(Folder / File, Columns, EventWindow, Read))
			{
				Result.Warnings.Add(FString::Printf(TEXT("%s has no CSV profiler header row (EVENTS,...) and was left out"), *File));
				continue;
			}
			if (!Groups.Contains(Group))
			{
				GroupOrder.Add(Group);
			}
			Groups.FindOrAdd(Group).Emplace(File, MoveTemp(Read));
		}
		TSet<FString> SeenColumns;
		for (const FString& GroupName : GroupOrder)
		{
			const TArray<TPair<FString, FCsvRun>>& Runs = Groups[GroupName];
			FAgentMcpPerfGroup& Group = Result.Groups.AddDefaulted_GetRef();
			Group.Name = GroupName;
			for (const TPair<FString, FCsvRun>& Run : Runs)
			{
				Group.Files.Add(Run.Key);
				Group.Frames += Run.Value.Frames;
			}
			for (const FString& Column : Columns)
			{
				TArray<double> Averages, P95s;
				for (const TPair<FString, FCsvRun>& Run : Runs)
				{
					if (const FColumnStats* Stats = Run.Value.Columns.Find(Column))
					{
						Averages.Add(Stats->Average);
						P95s.Add(Stats->P95);
					}
				}
				if (Averages.IsEmpty())
				{
					continue;
				}
				SeenColumns.Add(Column);
				FAgentMcpPerfStat& Stat = Group.Stats.AddDefaulted_GetRef();
				Stat.Column = Column;
				Stat.Median = Median(Averages);
				Stat.Spread = FMath::Max(Averages) - FMath::Min(Averages);
				Stat.P95 = Median(P95s);
				Stat.Runs = Averages.Num();
			}
			TMap<FString, TArray<FEventHit>> Events;
			for (const TPair<FString, FCsvRun>& Run : Runs)
			{
				for (const TPair<FString, TArray<FEventHit>>& Event : Run.Value.Events)
				{
					Events.FindOrAdd(Event.Key).Append(Event.Value);
				}
			}
			Events.KeySort(TLess<FString>());
			for (const TPair<FString, TArray<FEventHit>>& Event : Events)
			{
				TArray<double> Frames, Games;
				for (const FEventHit& Hit : Event.Value)
				{
					Frames.Add(Hit.Frame);
					Games.Add(Hit.GameThread);
				}
				FAgentMcpPerfEvent& Out = Group.Events.AddDefaulted_GetRef();
				Out.Name = Event.Key;
				Out.Count = Event.Value.Num();
				Out.FrameMedian = Median(Frames);
				Out.FrameMax = FMath::Max(Frames);
				Out.GameThreadMedian = Median(Games);
				Out.GameThreadMax = FMath::Max(Games);
			}
		}
		if (Files.IsEmpty())
		{
			Result.Warnings.Add(FString::Printf(TEXT("no file matches %s"), *Pattern));
		}
		for (const FString& Column : Columns)
		{
			if (!SeenColumns.Contains(Column) && !Result.Groups.IsEmpty())
			{
				Result.Warnings.Add(FString::Printf(TEXT("no file has the column %s"), *Column));
			}
		}
		return Result;
	}

	/** One ProfileGPU dump: its total and its pass lines, from the line after "Perf marker hierarchy" to the first line that is not one. */
	FAgentMcpGpuProfileResult ReadGpuDump(const TArray<FString>& Lines, int32 Occurrence, double MinMs, int32 MaxDepth)
	{
		FAgentMcpGpuProfileResult Result;
		TArray<int32> Starts;
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			if (Lines[i].Contains(TEXT("Perf marker hierarchy")))
			{
				Starts.Add(i);
			}
		}
		Result.Profiles = Starts.Num();
		if (Starts.IsEmpty())
		{
			return Result;
		}
		const int32 Start = Starts[Occurrence < 0 ? FMath::Max(0, Starts.Num() + Occurrence) : FMath::Min(Occurrence, Starts.Num() - 1)];
		const FRegexPattern Prefix(TEXT("^\\[[^\\]]*\\]\\[[^\\]]*\\]\\s*LogRHI:\\s?"));
		auto Body = [&Prefix](const FString& Line)
		{
			FRegexMatcher Matcher(Prefix, Line);
			return Matcher.FindNext() ? Line.RightChop(Matcher.GetMatchEnding()) : Line;
		};
		{
			FRegexMatcher Total(FRegexPattern(TEXT("total GPU time ([0-9.]+)ms")), Lines[Start]);
			if (Total.FindNext())
			{
				Result.TotalMs = FCString::Atod(*Total.GetCaptureGroup(1));
			}
		}
		const FRegexPattern PassLine(TEXT("^( *)\\s*[0-9.]+%\\s*([0-9.]+)ms\\s+(.*)$"));
		for (int32 i = Start + 1; i < Lines.Num(); ++i)
		{
			const FString Text = Body(Lines[i]);
			FRegexMatcher Matcher(PassLine, Text);
			if (!Matcher.FindNext())
			{
				if (Text.TrimStartAndEnd().IsEmpty())
				{
					continue;
				}
				break;
			}
			const int32 Depth = Matcher.GetCaptureGroup(1).Len() / 3;
			const double Ms = FCString::Atod(*Matcher.GetCaptureGroup(2));
			if (Ms >= MinMs && Depth <= MaxDepth)
			{
				FAgentMcpGpuPass& Pass = Result.Passes.AddDefaulted_GetRef();
				Pass.Depth = Depth;
				Pass.Ms = Ms;
				Pass.Name = Matcher.GetCaptureGroup(3).TrimEnd();
			}
		}
		return Result;
	}

	/** Timing events of one thread exported by Insights: TimerName,StartTime,EndTime,Depth. */
	struct FTimingEvent
	{
		FString Name;
		double Start = 0.0;
		double End = 0.0;
		int32 Depth = 0;
	};

	void VisitTimingEvents(const FString& File, TFunctionRef<void(const FTimingEvent&)> Visit)
	{
		bool bHeader = true;
		FTimingEvent Event;
		TArray<FString> Fields;
		FFileHelper::LoadFileToStringWithLineVisitor(*File, [&](FStringView Line)
		{
			if (bHeader)
			{
				bHeader = false;
				return;
			}
			// The timer name may hold commas; the three numbers are always the last fields.
			int32 Comma3 = INDEX_NONE, Comma2 = INDEX_NONE, Comma1 = INDEX_NONE;
			Line.FindLastChar(TEXT(','), Comma3);
			if (Comma3 == INDEX_NONE) return;
			Line.Left(Comma3).FindLastChar(TEXT(','), Comma2);
			if (Comma2 == INDEX_NONE) return;
			Line.Left(Comma2).FindLastChar(TEXT(','), Comma1);
			if (Comma1 == INDEX_NONE) return;
			Event.Name = FString(Line.Left(Comma1));
			Event.Start = FCString::Atod(*FString(Line.Mid(Comma1 + 1, Comma2 - Comma1 - 1)));
			Event.End = FCString::Atod(*FString(Line.Mid(Comma2 + 1, Comma3 - Comma2 - 1)));
			Event.Depth = FCString::Atoi(*FString(Line.Mid(Comma3 + 1)));
			Visit(Event);
		});
	}

	TArray<FAgentMcpPerfMarker> AnalyzeTimingEvents(const FString& File, const FString& MarkerPrefix, const FString& FrameTimer, int32 WindowFrames, int32 Top, bool bSkipFirst)
	{
		TArray<TPair<double, double>> Ticks;
		TArray<TPair<double, FString>> Marks;
		VisitTimingEvents(File, [&](const FTimingEvent& Event)
		{
			if (Event.Depth == 0 && Event.Name == FrameTimer)
			{
				Ticks.Emplace(Event.Start, Event.End);
			}
			else if (Event.Name.StartsWith(MarkerPrefix))
			{
				Marks.Emplace(Event.Start, Event.Name.RightChop(MarkerPrefix.Len()));
			}
		});
		if (Ticks.IsEmpty() || Marks.IsEmpty())
		{
			return {};
		}
		struct FWindow
		{
			double Start;
			double End;
			FString Marker;
		};
		TArray<FWindow> Windows;
		TMap<FString, int32> Seen;
		for (const TPair<double, FString>& Mark : Marks)
		{
			if (bSkipFirst && Seen.FindOrAdd(Mark.Value)++ == 0)
			{
				continue;
			}
			int32 Tick = Algo::UpperBoundBy(Ticks, Mark.Key, [](const TPair<double, double>& T) { return T.Key; }) - 1;
			Tick = FMath::Max(0, Tick);
			Windows.Add({Ticks[Tick].Key, Ticks[FMath::Min(Tick + WindowFrames - 1, Ticks.Num() - 1)].Value, Mark.Value});
		}
		Windows.Sort([](const FWindow& A, const FWindow& B) { return A.Start < B.Start; });
		TMap<FString, int32> Counts;
		for (const FWindow& Window : Windows)
		{
			++Counts.FindOrAdd(Window.Marker);
		}
		TMap<TPair<FString, FString>, double> Inclusive, Exclusive;
		struct FOpen
		{
			int32 Depth;
			FString Name;
			double Ms;
			double ChildrenMs;
			const FWindow* Window;
		};
		TArray<FOpen> Stack;
		auto Close = [&Exclusive](const FOpen& Open)
		{
			if (Open.Window)
			{
				Exclusive.FindOrAdd({Open.Window->Marker, Open.Name}) += Open.Ms - Open.ChildrenMs;
			}
		};
		VisitTimingEvents(File, [&](const FTimingEvent& Event)
		{
			while (Stack.Num() && Stack.Last().Depth >= Event.Depth)
			{
				Close(Stack.Pop(EAllowShrinking::No));
			}
			const int32 Index = Algo::UpperBoundBy(Windows, Event.Start, [](const FWindow& W) { return W.Start; }) - 1;
			const FWindow* Window = Index >= 0 && Event.Start <= Windows[Index].End ? &Windows[Index] : nullptr;
			const double Ms = (Event.End - Event.Start) * 1000.0;
			if (Stack.Num())
			{
				Stack.Last().ChildrenMs += Ms;
			}
			if (Window)
			{
				Inclusive.FindOrAdd({Window->Marker, Event.Name}) += Ms;
			}
			Stack.Add({Event.Depth, Event.Name, Ms, 0.0, Window});
		});
		while (Stack.Num())
		{
			Close(Stack.Pop(EAllowShrinking::No));
		}
		TArray<FAgentMcpPerfMarker> Markers;
		Counts.KeySort(TLess<FString>());
		for (const TPair<FString, int32>& Count : Counts)
		{
			FAgentMcpPerfMarker& Marker = Markers.AddDefaulted_GetRef();
			Marker.Name = Count.Key;
			Marker.Count = Count.Value;
			for (const TPair<TPair<FString, FString>, double>& Self : Exclusive)
			{
				// Idle waits are time the thread did nothing, not a cause.
				if (Self.Key.Key != Count.Key || Self.Key.Value == TEXT("Game thread idle time"))
				{
					continue;
				}
				FAgentMcpPerfTimer& Timer = Marker.Timers.AddDefaulted_GetRef();
				Timer.Name = Self.Key.Value.IsEmpty() ? TEXT("(unnamed)") : Self.Key.Value;
				Timer.SelfMs = Self.Value / Count.Value;
				Timer.InclusiveMs = Inclusive.FindRef(Self.Key) / Count.Value;
			}
			Marker.Timers.Sort([](const FAgentMcpPerfTimer& A, const FAgentMcpPerfTimer& B) { return A.SelfMs > B.SelfMs; });
			if (Marker.Timers.Num() > Top)
			{
				Marker.Timers.SetNum(Top);
			}
		}
		return Markers;
	}

	/** State of one perf_trace_hotspots call across the frames it takes. */
	struct FHotspotsJob
	{
		FAgentMcpPerfHotspotsResult Result;
		FProcHandle Insights;
		TFuture<TArray<FAgentMcpPerfMarker>> Analysis;
		FString InsightsLog;
	};
}

FAgentMcpPerfSummaryResult UAgentMcpPerfTools::SummarizeCsv(const FString& Folder, const FString& Pattern, const FString& Columns, int32 EventWindowFrames)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::PerfToolsPrivate;
	FString Full;
	if (!ResolveProjectPath(Folder, TEXT("folder"), true, Full))
	{
		return {};
	}
	return Summarize(Full, Pattern, ParseColumns(Columns), FMath::Clamp(EventWindowFrames, 1, 30));
}

FAgentMcpPerfCompareResult UAgentMcpPerfTools::CompareCsv(const FString& BaselineFolder, const FString& CandidateFolder, const FString& Pattern, const FString& Columns,
                                                         int32 EventWindowFrames)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::PerfToolsPrivate;
	FString Baseline, Candidate;
	if (!ResolveProjectPath(BaselineFolder, TEXT("baselineFolder"), true, Baseline) || !ResolveProjectPath(CandidateFolder, TEXT("candidateFolder"), true, Candidate))
	{
		return {};
	}
	const TArray<FString> ColumnList = ParseColumns(Columns);
	const int32 Window = FMath::Clamp(EventWindowFrames, 1, 30);
	FAgentMcpPerfCompareResult Result;
	Result.Baseline = Summarize(Baseline, Pattern, ColumnList, Window);
	Result.Candidate = Summarize(Candidate, Pattern, ColumnList, Window);
	for (const FAgentMcpPerfGroup& After : Result.Candidate.Groups)
	{
		const FAgentMcpPerfGroup* Before = Result.Baseline.Groups.FindByPredicate([&After](const FAgentMcpPerfGroup& G) { return G.Name == After.Name; });
		if (!Before)
		{
			continue;
		}
		for (const FAgentMcpPerfStat& Stat : After.Stats)
		{
			const FAgentMcpPerfStat* Old = Before->Stats.FindByPredicate([&Stat](const FAgentMcpPerfStat& S) { return S.Column == Stat.Column; });
			if (!Old)
			{
				continue;
			}
			FAgentMcpPerfStatChange& Change = Result.Stats.AddDefaulted_GetRef();
			Change.Group = After.Name;
			Change.Column = Stat.Column;
			Change.Baseline = Old->Median;
			Change.Candidate = Stat.Median;
			Change.Change = Stat.Median - Old->Median;
			Change.Noise = FMath::Max(Stat.Spread, Old->Spread);
			Change.bBeyondNoise = FMath::Abs(Change.Change) > Change.Noise;
		}
		for (const FAgentMcpPerfEvent& Event : After.Events)
		{
			const FAgentMcpPerfEvent* Old = Before->Events.FindByPredicate([&Event](const FAgentMcpPerfEvent& E) { return E.Name == Event.Name; });
			if (!Old)
			{
				continue;
			}
			FAgentMcpPerfEventChange& Change = Result.Events.AddDefaulted_GetRef();
			Change.Group = After.Name;
			Change.Event = Event.Name;
			Change.BaselineFrame = Old->FrameMedian;
			Change.CandidateFrame = Event.FrameMedian;
			Change.FrameChange = Event.FrameMedian - Old->FrameMedian;
			Change.BaselineGameThread = Old->GameThreadMedian;
			Change.CandidateGameThread = Event.GameThreadMedian;
			Change.GameThreadChange = Event.GameThreadMedian - Old->GameThreadMedian;
		}
	}
	return Result;
}

FAgentMcpGpuProfileResult UAgentMcpPerfTools::ReadGpuProfile(const FString& LogFile, double MinMs, int32 MaxDepth, int32 Occurrence)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::PerfToolsPrivate;
	FString Full;
	if (!ResolveProjectPath(LogFile, TEXT("logFile"), false, Full))
	{
		return {};
	}
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Full))
	{
		RaiseToolError(TEXT("NOT_AVAILABLE"), FString::Printf(TEXT("%s cannot be read."), *Full));
		return {};
	}
	FAgentMcpGpuProfileResult Result = ReadGpuDump(Lines, Occurrence, FMath::Max(0.0, MinMs), FMath::Clamp(MaxDepth, 0, 32));
	Result.LogFile = Full;
	if (Result.Profiles == 0)
	{
		RaiseToolError(TEXT("NOT_FOUND"), FString::Printf(TEXT("%s holds no ProfileGPU dump."), *Full),
		               TEXT("Run ProfileGPU in the game (a console command or -ExecCmds) while it shows what to measure, then read its log."));
		return {};
	}
	return Result;
}

UAgentMcpAsyncResult* UAgentMcpPerfTools::TraceHotspots(const FString& TraceFile, const FString& MarkerPrefix, const FString& Thread, const FString& FrameTimer,
                                                       int32 WindowFrames, int32 Top, bool bSkipFirst, float TimeoutSeconds)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::PerfToolsPrivate;
	FString Trace;
	if (!ResolveProjectPath(TraceFile, TEXT("traceFile"), false, Trace))
	{
		return nullptr;
	}
	if (MarkerPrefix.IsEmpty() || Thread.TrimStartAndEnd().IsEmpty())
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'markerPrefix' and 'thread' must not be empty."));
		return nullptr;
	}
	const TSharedRef<FHotspotsJob> Job = MakeShared<FHotspotsJob>();
	Job->Result.TraceFile = Trace;
	const FString Base = FPaths::GetPath(Trace) / FPaths::GetBaseFilename(Trace);
	Job->Result.ExportFile = FString::Printf(TEXT("%s_%s.csv"), *Base, *Thread.Replace(TEXT(" "), TEXT("")));
	const FDateTime TraceTime = IFileManager::Get().GetTimeStamp(*Trace);
	const FDateTime ExportTime = IFileManager::Get().GetTimeStamp(*Job->Result.ExportFile);
	if (ExportTime == FDateTime::MinValue() || ExportTime < TraceTime)
	{
		// The command line takes a value only up to its first space, so the export command goes in a response file.
		const FString Commands = Base + TEXT("_insights_commands.txt");
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("TimingInsights.ExportTimingEvents \"%s\" -columns=TimerName,StartTime,EndTime,Depth -threads=%s -timers=*\n"),
		                                              *Job->Result.ExportFile, *Thread), *Commands);
		Job->InsightsLog = Base + TEXT("_insights.log");
		const FString Insights = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/Win64/UnrealInsights.exe"));
		const FString Params = FString::Printf(TEXT("-OpenTraceFile=\"%s\" -ABSLOG=\"%s\" -AutoQuit -NoUI -ExecOnAnalysisCompleteCmd=\"@=%s\" -log"), *Trace, *Job->InsightsLog, *Commands);
		IFileManager::Get().Delete(*Job->Result.ExportFile, false, true, true);
		Job->Insights = FPlatformProcess::CreateProc(*Insights, *Params, true, true, true, nullptr, 0, nullptr, nullptr);
		if (!Job->Insights.IsValid())
		{
			RaiseToolError(TEXT("NOT_AVAILABLE"), FString::Printf(TEXT("%s could not be started."), *Insights));
			return nullptr;
		}
		Job->Result.bExported = true;
	}
	const FString Prefix = MarkerPrefix, Timer = FrameTimer;
	const int32 Window = FMath::Clamp(WindowFrames, 1, 30), Kept = FMath::Clamp(Top, 1, 200);
	return UAgentMcpAsyncResult::Create(FMath::Clamp(TimeoutSeconds, 30.0f, 3600.0f), [Job, Prefix, Timer, Window, Kept, bSkipFirst](UAgentMcpAsyncResult& Result)
	{
		if (Job->Insights.IsValid())
		{
			if (FPlatformProcess::IsProcRunning(Job->Insights))
			{
				return;
			}
			FPlatformProcess::CloseProc(Job->Insights);
			Job->Insights.Reset();
			if (!IFileManager::Get().FileExists(*Job->Result.ExportFile))
			{
				Result.Fail(TEXT("NOT_AVAILABLE"), TEXT("UnrealInsights.exe ended without exporting the timing events."),
				            FString::Printf(TEXT("Read %s; the trace may lack the cpu channel (-trace=cpu,frame)."), *Job->InsightsLog));
				return;
			}
		}
		if (!Job->Analysis.IsValid())
		{
			const FString File = Job->Result.ExportFile;
			Job->Analysis = Async(EAsyncExecution::Thread, [File, Prefix, Timer, Window, Kept, bSkipFirst]
			{
				return AnalyzeTimingEvents(File, Prefix, Timer, Window, Kept, bSkipFirst);
			});
			return;
		}
		if (!Job->Analysis.IsReady())
		{
			return;
		}
		Job->Result.Markers = Job->Analysis.Get();
		if (Job->Result.Markers.IsEmpty())
		{
			Result.Fail(TEXT("NOT_FOUND"), FString::Printf(TEXT("No scope named '%s...' with frames of '%s' was found in the trace."), *Prefix, *Timer),
			            TEXT("Mark moments with TRACE_CPUPROFILER_EVENT_SCOPE_TEXT and record with -trace=cpu,frame -statnamedevents."));
			return;
		}
		Result.CompleteWith(Job->Result);
	});
}
