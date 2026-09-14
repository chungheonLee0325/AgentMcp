#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Misc/DateTime.h"
#include "Misc/OutputDevice.h"

struct FAgentMcpLogEntry
{
	uint64 Sequence = 0;
	FDateTime TimestampUtc;
	FName Category;
	ELogVerbosity::Type Verbosity = ELogVerbosity::Log;
	FString Message;
};

struct FAgentMcpLogQuery
{
	/** Entries with Sequence > SinceSequence, oldest first. 0 returns the most recent MaxEntries matches. */
	uint64 SinceSequence = 0;

	/** Include entries at this verbosity or more severe (Fatal < Error < Warning < Display < Log < Verbose). */
	ELogVerbosity::Type MaxVerbosity = ELogVerbosity::Log;

	/** Empty means all categories. */
	TArray<FName> Categories;

	/** Case-insensitive substring filter. Empty means no filter. */
	FString Contains;

	int32 MaxEntries = 200;
};

struct FAgentMcpLogQueryResult
{
	TArray<FAgentMcpLogEntry> Entries;

	/** Pass as SinceSequence on the next query to continue. */
	uint64 NextSequence = 0;

	/** Oldest sequence still held by the buffer; older lines were dropped. */
	uint64 OldestAvailableSequence = 0;

	uint64 LatestSequence = 0;

	/** More matching entries exist than were returned. */
	bool bTruncated = false;
};

/** In-memory ring buffer of editor log lines with increasing sequence numbers, registered with GLog at module startup. */
class AGENTMCPTOOLSET_API FAgentMcpLogBuffer final : public FOutputDevice
{
public:
	static FAgentMcpLogBuffer& Get();

	void Register(int32 InCapacity);
	void Unregister();

	FAgentMcpLogQueryResult Query(const FAgentMcpLogQuery& InQuery) const;
	uint64 GetLatestSequence() const;

	//~ Begin FOutputDevice
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	//~ End FOutputDevice

private:
	mutable FCriticalSection Lock;
	TArray<FAgentMcpLogEntry> Ring;
	int32 Capacity = 0;
	int32 Head = 0;
	int32 Count = 0;
	uint64 NextSequence = 1;
	bool bRegistered = false;
};
