#include "AgentMcpStringTableTools.h"

#include "AgentMcpToolsCommon.h"

#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "UObject/Package.h"

namespace UE::AgentMcp::StringTableToolsPrivate
{
	constexpr int32 MaxEntryPage = 2000;

	TArray<FString> GetSortedKeys(const FStringTableConstRef& Table, const FString& KeyContains)
	{
		TArray<FString> Keys;
		Table->EnumerateSourceStrings([&Keys, &KeyContains](const FString& Key, const FString&)
		{
			if (Tools::MatchesNameFilter(Key, KeyContains))
			{
				Keys.Add(Key);
			}
			return true;
		});
		Keys.Sort();
		return Keys;
	}

	int32 CountEntries(const FStringTableConstRef& Table)
	{
		int32 Count = 0;
		Table->EnumerateSourceStrings([&Count](const FString&, const FString&)
		{
			++Count;
			return true;
		});
		return Count;
	}
}

FAgentMcpStringTableEntries UAgentMcpStringTableTools::GetEntries(UStringTable* StringTable, const FString& KeyContains, int32 Limit, int32 Cursor)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::StringTableToolsPrivate;

	FAgentMcpStringTableEntries Result;
	if (!Tools::RequireObject(StringTable, TEXT("stringTable")))
	{
		return Result;
	}

	const FStringTableConstRef Table = StringTable->GetStringTable();
	const TArray<FString> Keys = GetSortedKeys(Table, KeyContains);
	int32 Start = 0;
	int32 End = 0;
	int32 NextCursor = -1;
	if (!Tools::GetPage(Keys.Num(), Limit, Cursor, MaxEntryPage, Start, End, NextCursor))
	{
		return Result;
	}
	for (int32 Index = Start; Index < End; ++Index)
	{
		FString Source;
		Table->GetSourceString(Keys[Index], Source);
		Result.Entries.Add(Keys[Index], Source);
	}

	Result.StringTable = StringTable->GetPathName();
	Result.Namespace = Table->GetNamespace();
	Result.EntryCount = CountEntries(Table);
	Result.TotalMatched = Keys.Num();
	Result.NextCursor = NextCursor;
	return Result;
}

FAgentMcpStringTableEditResult UAgentMcpStringTableTools::SetEntries(UStringTable* StringTable, const TMap<FString, FString>& Entries, const FString& Namespace)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::StringTableToolsPrivate;

	FAgentMcpStringTableEditResult Result;
	if (!Tools::RequireObject(StringTable, TEXT("stringTable")) || !Tools::RequireProjectContent(StringTable))
	{
		return Result;
	}
	if (Entries.IsEmpty() && Namespace.IsEmpty())
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'entries' must hold at least one entry, or 'namespace' must be given."),
			TEXT("Pass {\"Key\": \"Source string\"}, for example {\"Result.Success\": \"Victory\"}."));
		return Result;
	}

	// Check every key and string before changing anything, so a bad entry leaves the table untouched.
	TArray<FString> Problems;
	TSet<FString> ProblemCodes;
	for (const TPair<FString, FString>& Entry : Entries)
	{
		if (Entry.Key.TrimStartAndEnd().IsEmpty())
		{
			Problems.Add(TEXT("a key is empty"));
			ProblemCodes.Add(TEXT("INVALID_ARGUMENT"));
		}
		else if (Entry.Key != Entry.Key.TrimStartAndEnd())
		{
			Problems.Add(FString::Printf(TEXT("key '%s' starts or ends with whitespace"), *Entry.Key.Left(128)));
			ProblemCodes.Add(TEXT("INVALID_ARGUMENT"));
		}
		else if (Entry.Value.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("the source string of '%s' is empty"), *Entry.Key.Left(128)));
			ProblemCodes.Add(TEXT("INVALID_ARGUMENT"));
		}
	}
	if (!Problems.IsEmpty())
	{
		Tools::RaiseProblems(FString::Printf(TEXT("Nothing was changed in %s"), *StringTable->GetPathName()), Problems, ProblemCodes,
			TEXT("stringtable_get_entries lists the keys the table has."));
		return Result;
	}

	const FStringTableRef Table = StringTable->GetMutableStringTable();
	const bool bChangeNamespace = !Namespace.IsEmpty() && Namespace != Table->GetNamespace();
	TArray<TPair<FString, FString>> ToChange;
	for (const TPair<FString, FString>& Entry : Entries)
	{
		FString Current;
		if (Table->GetSourceString(Entry.Key, Current) && Current.Equals(Entry.Value, ESearchCase::CaseSensitive))
		{
			Result.Unchanged.Add(Entry.Key);
		}
		else
		{
			ToChange.Emplace(Entry.Key, Entry.Value);
		}
	}

	if (bChangeNamespace || !ToChange.IsEmpty())
	{
		// Record the table for undo; the dispatcher's transaction wraps the call.
		StringTable->Modify();
		if (bChangeNamespace)
		{
			Table->SetNamespace(Namespace);
			Result.bNamespaceChanged = true;
		}
		for (const TPair<FString, FString>& Entry : ToChange)
		{
			Table->SetSourceString(Entry.Key, Entry.Value);
			Result.Changed.Add(Entry.Key);
		}
	}

	Result.StringTable = StringTable->GetPathName();
	Result.Namespace = Table->GetNamespace();
	Result.EntryCount = CountEntries(Table);
	Result.Warnings.Append(Tools::GetSourceControlWarnings(StringTable->GetPackage()));
	return Result;
}
