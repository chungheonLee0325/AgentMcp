#include "AgentMcpLogBuffer.h"

#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"

namespace UE::AgentMcp::LogBufferPrivate
{
	constexpr int32 MaxMessageChars = 4096;
	constexpr int32 MaxQueryEntries = 2000;
}

FAgentMcpLogBuffer& FAgentMcpLogBuffer::Get()
{
	static FAgentMcpLogBuffer Instance;
	return Instance;
}

void FAgentMcpLogBuffer::Register(int32 InCapacity)
{
	{
		FScopeLock ScopeLock(&Lock);
		Capacity = FMath::Max(100, InCapacity);
		Ring.Reset();
		Ring.SetNum(Capacity);
		Head = 0;
		Count = 0;
	}

	if (!bRegistered && GLog)
	{
		GLog->AddOutputDevice(this);
		bRegistered = true;
	}
}

void FAgentMcpLogBuffer::Unregister()
{
	if (bRegistered && GLog)
	{
		GLog->RemoveOutputDevice(this);
	}
	bRegistered = false;
}

void FAgentMcpLogBuffer::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
	if (V == nullptr || Verbosity == ELogVerbosity::SetColor)
	{
		return;
	}

	const ELogVerbosity::Type Level = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
	if (Level == ELogVerbosity::NoLogging)
	{
		return;
	}

	FString Text(V);
	if (Text.Len() > UE::AgentMcp::LogBufferPrivate::MaxMessageChars)
	{
		Text.LeftInline(UE::AgentMcp::LogBufferPrivate::MaxMessageChars);
		Text += TEXT("...");
	}

	FScopeLock ScopeLock(&Lock);
	if (Capacity <= 0)
	{
		return;
	}

	int32 Index = INDEX_NONE;
	if (Count < Capacity)
	{
		Index = (Head + Count) % Capacity;
		++Count;
	}
	else
	{
		Index = Head;
		Head = (Head + 1) % Capacity;
	}

	FAgentMcpLogEntry& Entry = Ring[Index];
	Entry.Sequence = NextSequence++;
	Entry.TimestampUtc = FDateTime::UtcNow();
	Entry.Category = Category;
	Entry.Verbosity = Level;
	Entry.Message = MoveTemp(Text);
}

uint64 FAgentMcpLogBuffer::GetLatestSequence() const
{
	FScopeLock ScopeLock(&Lock);
	return NextSequence - 1;
}

FAgentMcpLogQueryResult FAgentMcpLogBuffer::Query(const FAgentMcpLogQuery& InQuery) const
{
	FAgentMcpLogQueryResult Result;
	const int32 MaxEntries = FMath::Clamp(InQuery.MaxEntries, 1, UE::AgentMcp::LogBufferPrivate::MaxQueryEntries);

	FScopeLock ScopeLock(&Lock);
	Result.LatestSequence = NextSequence - 1;
	Result.OldestAvailableSequence = Count > 0 ? Ring[Head].Sequence : NextSequence;

	auto Matches = [&InQuery](const FAgentMcpLogEntry& Entry)
	{
		if (Entry.Verbosity > InQuery.MaxVerbosity)
		{
			return false;
		}
		if (InQuery.Categories.Num() > 0 && !InQuery.Categories.Contains(Entry.Category))
		{
			return false;
		}
		if (!InQuery.Contains.IsEmpty() && !Entry.Message.Contains(InQuery.Contains, ESearchCase::IgnoreCase))
		{
			return false;
		}
		return true;
	};

	if (InQuery.SinceSequence == 0)
	{
		// Tail mode: newest matches, returned oldest first.
		TArray<int32> SelectedIndices;
		for (int32 Offset = Count - 1; Offset >= 0; --Offset)
		{
			const int32 Index = (Head + Offset) % Capacity;
			if (!Matches(Ring[Index]))
			{
				continue;
			}
			if (SelectedIndices.Num() == MaxEntries)
			{
				Result.bTruncated = true;
				break;
			}
			SelectedIndices.Add(Index);
		}

		Result.Entries.Reserve(SelectedIndices.Num());
		for (int32 Position = SelectedIndices.Num() - 1; Position >= 0; --Position)
		{
			Result.Entries.Add(Ring[SelectedIndices[Position]]);
		}
		Result.NextSequence = Result.LatestSequence;
	}
	else
	{
		// Incremental mode: everything after SinceSequence, oldest first.
		for (int32 Offset = 0; Offset < Count; ++Offset)
		{
			const FAgentMcpLogEntry& Entry = Ring[(Head + Offset) % Capacity];
			if (Entry.Sequence <= InQuery.SinceSequence || !Matches(Entry))
			{
				continue;
			}
			if (Result.Entries.Num() == MaxEntries)
			{
				Result.bTruncated = true;
				break;
			}
			Result.Entries.Add(Entry);
		}
		Result.NextSequence = (Result.bTruncated && Result.Entries.Num() > 0) ? Result.Entries.Last().Sequence : Result.LatestSequence;
	}

	return Result;
}
