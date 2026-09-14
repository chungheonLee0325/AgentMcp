#include "AgentMcpLogTools.h"

#include "AgentMcpLogBuffer.h"

namespace UE::AgentMcp::LogToolsPrivate
{
	ELogVerbosity::Type ToEngineVerbosity(EAgentMcpLogVerbosity Verbosity)
	{
		switch (Verbosity)
		{
		case EAgentMcpLogVerbosity::Fatal: return ELogVerbosity::Fatal;
		case EAgentMcpLogVerbosity::Error: return ELogVerbosity::Error;
		case EAgentMcpLogVerbosity::Warning: return ELogVerbosity::Warning;
		case EAgentMcpLogVerbosity::Display: return ELogVerbosity::Display;
		case EAgentMcpLogVerbosity::Verbose: return ELogVerbosity::Verbose;
		case EAgentMcpLogVerbosity::VeryVerbose: return ELogVerbosity::VeryVerbose;
		case EAgentMcpLogVerbosity::Log:
		default: return ELogVerbosity::Log;
		}
	}
}

FAgentMcpLogRecentResult UAgentMcpLogTools::GetRecent(int64 SinceSequence, EAgentMcpLogVerbosity MinVerbosity, const FString& Categories, const FString& Contains, int32 MaxEntries)
{
	FAgentMcpLogRecentResult Result;

	FAgentMcpLogQuery Query;
	Query.SinceSequence = SinceSequence > 0 ? static_cast<uint64>(SinceSequence) : 0;
	Query.MaxVerbosity = UE::AgentMcp::LogToolsPrivate::ToEngineVerbosity(MinVerbosity);
	Query.MaxEntries = FMath::Clamp(MaxEntries, 1, 2000);
	Query.Contains = Contains.TrimStartAndEnd();

	TArray<FString> CategoryNames;
	Categories.ParseIntoArray(CategoryNames, TEXT(","), /*bCullEmpty=*/true);
	for (const FString& CategoryName : CategoryNames)
	{
		const FString Trimmed = CategoryName.TrimStartAndEnd();
		if (!Trimmed.IsEmpty())
		{
			Query.Categories.Add(FName(*Trimmed));
		}
	}

	const FAgentMcpLogQueryResult QueryResult = FAgentMcpLogBuffer::Get().Query(Query);

	Result.Entries.Reserve(QueryResult.Entries.Num());
	for (const FAgentMcpLogEntry& Entry : QueryResult.Entries)
	{
		FAgentMcpLogLine& Line = Result.Entries.AddDefaulted_GetRef();
		Line.Sequence = static_cast<int64>(Entry.Sequence);
		Line.Time = Entry.TimestampUtc.ToIso8601();
		Line.Category = Entry.Category.ToString();
		Line.Verbosity = ::ToString(Entry.Verbosity);
		Line.Message = Entry.Message;
	}
	Result.NextSequence = static_cast<int64>(QueryResult.NextSequence);
	Result.OldestAvailableSequence = static_cast<int64>(QueryResult.OldestAvailableSequence);
	Result.bTruncated = QueryResult.bTruncated;
	return Result;
}
