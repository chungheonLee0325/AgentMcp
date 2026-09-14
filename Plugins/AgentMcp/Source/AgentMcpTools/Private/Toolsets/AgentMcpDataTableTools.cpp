#include "AgentMcpDataTableTools.h"

#include "AgentMcpJson.h"
#include "AgentMcpToolsCommon.h"

#include "DataTableEditorUtils.h"
#include "Engine/DataTable.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace UE::AgentMcp::DataTableToolsPrivate
{
	constexpr int32 MaxRowNamePage = 2000;
	constexpr int32 MaxRowValuePage = 500;

	TArray<FProperty*> GetColumns(const UScriptStruct* RowStruct)
	{
		TArray<FProperty*> Columns;
		for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Deprecated))
			{
				Columns.Add(*It);
			}
		}
		return Columns;
	}

	/** User-defined struct properties carry generated suffixes; the authored name is what the DataTable editor shows. */
	FString GetColumnName(const FProperty* Property)
	{
		return Property->GetAuthoredName();
	}

	FProperty* FindColumn(const TArray<FProperty*>& Columns, const FString& Name)
	{
		const FString Trimmed = Name.TrimStartAndEnd();
		FProperty* const* Found = Columns.FindByPredicate([&Trimmed](const FProperty* Property)
		{
			return GetColumnName(Property).Equals(Trimmed, ESearchCase::IgnoreCase) || Property->GetName().Equals(Trimmed, ESearchCase::IgnoreCase);
		});
		return Found ? *Found : nullptr;
	}

	const UScriptStruct* RequireRowStruct(const UDataTable* DataTable)
	{
		if (!Tools::RequireObject(DataTable, TEXT("dataTable")))
		{
			return nullptr;
		}
		const UScriptStruct* RowStruct = DataTable->GetRowStruct();
		if (!RowStruct)
		{
			RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("%s has no row struct; the struct may have been renamed or deleted."), *DataTable->GetPathName()));
		}
		return RowStruct;
	}

	TSharedRef<FJsonObject> ReadRow(const TArray<FProperty*>& Columns, const uint8* RowData)
	{
		const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		for (const FProperty* Property : Columns)
		{
			const TSharedPtr<FJsonValue> Value = PropertyValueToJson(Property, Property->ContainerPtrToValuePtr<void>(RowData));
			if (Value.IsValid())
			{
				Row->SetField(GetColumnName(Property), Value);
			}
			else
			{
				Row->SetField(GetColumnName(Property), MakeShared<FJsonValueNull>());
			}
		}
		return Row;
	}

	/** Finds an existing row. A name that was never created cannot name a row, so the name table is not extended. */
	uint8* FindRow(const UDataTable* DataTable, const FString& RowName, FName& OutName)
	{
		const FString Trimmed = RowName.TrimStartAndEnd();
		OutName = (Trimmed.IsEmpty() || Trimmed.Len() >= NAME_SIZE) ? FName() : FName(*Trimmed, FNAME_Find);
		if (OutName.IsNone())
		{
			return nullptr;
		}
		uint8* const* RowData = DataTable->GetRowMap().Find(OutName);
		return RowData ? *RowData : nullptr;
	}

	bool MakeNewRowName(const FString& RowName, FName& OutName, FString& OutProblem)
	{
		const FString Trimmed = RowName.TrimStartAndEnd();
		FText Reason;
		if (Trimmed.IsEmpty() || Trimmed.Len() >= NAME_SIZE || !FName::IsValidXName(Trimmed, INVALID_NAME_CHARACTERS, &Reason))
		{
			OutProblem = FString::Printf(TEXT("'%s' is not a valid row name%s%s"), *Trimmed.Left(64), Reason.IsEmpty() ? TEXT("") : TEXT(": "), *Reason.ToString());
			return false;
		}
		OutName = FName(*Trimmed);
		return true;
	}

	/** Row values converted from JSON before the table changes. */
	class FStagedRows
	{
	public:
		explicit FStagedRows(const UScriptStruct* InRowStruct)
			: RowStruct(InRowStruct)
		{
		}

		FStagedRows(const FStagedRows&) = delete;
		FStagedRows& operator=(const FStagedRows&) = delete;

		~FStagedRows()
		{
			for (const TPair<FName, uint8*>& Row : Rows)
			{
				RowStruct->DestroyStruct(Row.Value);
				FMemory::Free(Row.Value);
			}
		}

		/** Stages a row that starts from Source (an existing row) or from the struct defaults. */
		uint8* Add(FName RowName, const uint8* Source)
		{
			uint8* Memory = static_cast<uint8*>(FMemory::Malloc(static_cast<SIZE_T>(FMath::Max(RowStruct->GetStructureSize(), 1)), static_cast<uint32>(RowStruct->GetMinAlignment())));
			RowStruct->InitializeStruct(Memory);
			if (Source)
			{
				RowStruct->CopyScriptStruct(Memory, Source);
			}
			Rows.Emplace(RowName, Memory);
			return Memory;
		}

		uint8* Find(const FString& RowName) const
		{
			const FString Trimmed = RowName.TrimStartAndEnd();
			const TPair<FName, uint8*>* Row = Rows.FindByPredicate([&Trimmed](const TPair<FName, uint8*>& Candidate)
			{
				return Candidate.Key.ToString().Equals(Trimmed, ESearchCase::IgnoreCase);
			});
			return Row ? Row->Value : nullptr;
		}

		const TArray<TPair<FName, uint8*>>& GetRows() const { return Rows; }

	private:
		const UScriptStruct* RowStruct = nullptr;
		TArray<TPair<FName, uint8*>> Rows;
	};

	struct FProblems
	{
		TArray<FString> Messages;
		TSet<FString> Codes;

		void Add(const FString& Code, const FString& Message)
		{
			Codes.Add(Code);
			// Messages are joined into one sentence, so drop their own final period.
			FString Text = Message;
			Text.RemoveFromEnd(TEXT("."));
			Messages.Add(MoveTemp(Text));
		}

		/** Raises one error listing every problem. Returns true when there were problems. */
		bool Raise(const UDataTable* DataTable) const
		{
			if (Messages.IsEmpty())
			{
				return false;
			}
			const FString Code = Codes.Num() == 1 ? *Codes.CreateConstIterator() : FString(TEXT("INVALID_ARGUMENT"));
			RaiseToolError(Code, FString::Printf(TEXT("Nothing was changed in %s: %s."), *DataTable->GetPathName(), *FString::Join(Messages, TEXT("; "))),
				TEXT("datatable_get_schema lists the columns and their types; datatable_list_rows lists the rows."));
			return true;
		}
	};

	/** Converts an object of column values into a staged row. */
	void StageColumnValues(const TArray<FProperty*>& Columns, const FString& RowLabel, const TSharedPtr<FJsonValue>& ValuesJson, uint8* RowMemory, FProblems& Problems)
	{
		const TSharedPtr<FJsonObject>* ValuesObject = nullptr;
		if (!ValuesJson.IsValid() || !ValuesJson->TryGetObject(ValuesObject) || !ValuesObject || !ValuesObject->IsValid())
		{
			Problems.Add(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("the values of row '%s' must be an object of column values"), *RowLabel));
			return;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ValuesObject)->Values)
		{
			FProperty* Column = FindColumn(Columns, Pair.Key);
			if (!Column)
			{
				Problems.Add(TEXT("UNKNOWN_COLUMN"), FString::Printf(TEXT("row '%s' has no column '%s'"), *RowLabel, *Pair.Key));
				continue;
			}
			FString ErrorCode;
			FString Error;
			if (!JsonToPropertyValue(Pair.Value, Column, Column->ContainerPtrToValuePtr<void>(RowMemory), RowLabel + TEXT(".") + GetColumnName(Column), ErrorCode, Error))
			{
				Problems.Add(ErrorCode, Error);
			}
		}
	}

	/** Common checks of the editing tools: a row struct, and a DataTable that belongs to the project. */
	const UScriptStruct* RequireEditableTable(const UDataTable* DataTable)
	{
		const UScriptStruct* RowStruct = RequireRowStruct(DataTable);
		return RowStruct && Tools::RequireProjectContent(DataTable) ? RowStruct : nullptr;
	}

	void FinishEdit(UDataTable* DataTable, const TArray<FProperty*>& Columns, const TArray<FName>& ReadBackRows, FAgentMcpDataTableEditResult& Result)
	{
		Result.DataTable = DataTable->GetPathName();
		Result.RowCount = DataTable->GetRowMap().Num();
		Result.Warnings.Append(Tools::GetSourceControlWarnings(DataTable->GetPackage()));
		if (ReadBackRows.IsEmpty())
		{
			return;
		}

		const TSharedRef<FJsonObject> After = MakeShared<FJsonObject>();
		for (const FName RowName : ReadBackRows)
		{
			if (uint8* const* RowData = DataTable->GetRowMap().Find(RowName); RowData && *RowData)
			{
				After->SetObjectField(RowName.ToString(), ReadRow(Columns, *RowData));
			}
		}
		Result.After.JsonObject = After;
	}
}

FAgentMcpDataTableSchema UAgentMcpDataTableTools::GetSchema(UDataTable* DataTable)
{
	using namespace UE::AgentMcp::DataTableToolsPrivate;

	FAgentMcpDataTableSchema Result;
	const UScriptStruct* RowStruct = RequireRowStruct(DataTable);
	if (!RowStruct)
	{
		return Result;
	}

	const bool bNative = (RowStruct->StructFlags & STRUCT_Native) != 0;
	Result.DataTable = DataTable->GetPathName();
	Result.RowStruct = RowStruct->GetPathName();
	Result.bUserDefinedStruct = !bNative;
	if (bNative)
	{
		Result.RowStructModule = RowStruct->GetPackage()->GetName();
		Result.RowStructHeader = RowStruct->GetMetaData(TEXT("ModuleRelativePath"));
	}
	Result.RowCount = DataTable->GetRowMap().Num();

	for (const FProperty* Property : GetColumns(RowStruct))
	{
		FAgentMcpDataTableColumn& Column = Result.Columns.AddDefaulted_GetRef();
		Column.Name = GetColumnName(Property);
		if (Property->GetName() != Column.Name)
		{
			Column.PropertyName = Property->GetName();
		}
		FString ExtendedType;
		Column.Type = Property->GetCPPType(&ExtendedType) + ExtendedType;
		Column.Category = Property->GetMetaData(TEXT("Category"));
		Column.Description = UE::AgentMcp::Tools::GetShortDescription(Property->GetMetaData(TEXT("ToolTip")));
		Column.JsonSchema.JsonObject = UE::AgentMcp::PropertyToJsonSchema(Property);
	}
	return Result;
}

FAgentMcpDataTableRowList UAgentMcpDataTableTools::ListRows(UDataTable* DataTable, const FString& NameContains, int32 Limit, int32 Cursor)
{
	using namespace UE::AgentMcp::DataTableToolsPrivate;

	FAgentMcpDataTableRowList Result;
	if (!RequireRowStruct(DataTable))
	{
		return Result;
	}
	Result.DataTable = DataTable->GetPathName();
	Result.RowCount = DataTable->GetRowMap().Num();

	const FString NameFilter = NameContains.TrimStartAndEnd();
	TArray<FString> Matches;
	for (const TPair<FName, uint8*>& Row : DataTable->GetRowMap())
	{
		FString RowName = Row.Key.ToString();
		if (UE::AgentMcp::Tools::MatchesNameFilter(RowName, NameFilter))
		{
			Matches.Add(MoveTemp(RowName));
		}
	}

	int32 Start = 0;
	int32 End = 0;
	int32 NextCursor = -1;
	if (!UE::AgentMcp::Tools::GetPage(Matches.Num(), Limit, Cursor, MaxRowNamePage, Start, End, NextCursor))
	{
		return Result;
	}
	Result.TotalMatched = Matches.Num();
	Result.NextCursor = NextCursor;
	for (int32 Index = Start; Index < End; ++Index)
	{
		Result.Rows.Add(MoveTemp(Matches[Index]));
	}
	return Result;
}

FAgentMcpDataTableRows UAgentMcpDataTableTools::GetRows(UDataTable* DataTable, const TArray<FString>& RowNames, const TArray<FString>& ColumnNames, int32 Limit, int32 Cursor)
{
	using namespace UE::AgentMcp::DataTableToolsPrivate;

	FAgentMcpDataTableRows Result;
	const UScriptStruct* RowStruct = RequireRowStruct(DataTable);
	if (!RowStruct)
	{
		return Result;
	}
	Result.DataTable = DataTable->GetPathName();
	const TMap<FName, uint8*>& RowMap = DataTable->GetRowMap();
	Result.RowCount = RowMap.Num();

	const TArray<FProperty*> AllColumns = GetColumns(RowStruct);
	TArray<FProperty*> Columns;
	if (ColumnNames.IsEmpty())
	{
		Columns = AllColumns;
	}
	else
	{
		for (const FString& Requested : ColumnNames)
		{
			if (FProperty* Column = FindColumn(AllColumns, Requested))
			{
				Columns.AddUnique(Column);
			}
			else
			{
				Result.MissingColumns.Add(Requested);
			}
		}
	}

	const TSharedRef<FJsonObject> Rows = MakeShared<FJsonObject>();
	if (RowNames.IsEmpty())
	{
		int32 Start = 0;
		int32 End = 0;
		int32 NextCursor = -1;
		if (!UE::AgentMcp::Tools::GetPage(RowMap.Num(), Limit, Cursor, MaxRowValuePage, Start, End, NextCursor))
		{
			return Result;
		}
		Result.NextCursor = NextCursor;

		int32 Index = 0;
		for (const TPair<FName, uint8*>& Row : RowMap)
		{
			if (Index >= End)
			{
				break;
			}
			if (Index >= Start && Row.Value)
			{
				Rows->SetObjectField(Row.Key.ToString(), ReadRow(Columns, Row.Value));
			}
			++Index;
		}
	}
	else
	{
		for (const FString& Requested : RowNames)
		{
			FName RowName;
			if (const uint8* RowData = FindRow(DataTable, Requested, RowName))
			{
				Rows->SetObjectField(RowName.ToString(), ReadRow(Columns, RowData));
			}
			else
			{
				Result.MissingRows.Add(Requested);
			}
		}
	}

	Result.Rows.JsonObject = Rows;
	return Result;
}

FAgentMcpDataTableEditResult UAgentMcpDataTableTools::SetRows(UDataTable* DataTable, const FJsonObjectWrapper& Rows)
{
	using namespace UE::AgentMcp::DataTableToolsPrivate;

	FAgentMcpDataTableEditResult Result;
	const UScriptStruct* RowStruct = RequireEditableTable(DataTable);
	if (!RowStruct)
	{
		return Result;
	}
	const TSharedPtr<FJsonObject>& Requested = Rows.JsonObject;
	if (!Requested.IsValid() || Requested->Values.IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'rows' must contain at least one row."), TEXT("datatable_list_rows lists the rows."));
		return Result;
	}

	const TArray<FProperty*> Columns = GetColumns(RowStruct);
	FStagedRows Staged(RowStruct);
	FProblems Problems;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Requested->Values)
	{
		FName RowName;
		const uint8* Existing = FindRow(DataTable, Pair.Key, RowName);
		if (!Existing)
		{
			Problems.Add(TEXT("UNKNOWN_ROW"), FString::Printf(TEXT("row '%s' does not exist"), *Pair.Key));
			continue;
		}
		StageColumnValues(Columns, RowName.ToString(), Pair.Value, Staged.Add(RowName, Existing), Problems);
	}
	if (Problems.Raise(DataTable))
	{
		return Result;
	}

	TArray<FName> Changed;
	FDataTableEditorUtils::BroadcastPreChange(DataTable, FDataTableEditorUtils::EDataTableChangeInfo::RowData);
	DataTable->Modify();
	for (const TPair<FName, uint8*>& Row : Staged.GetRows())
	{
		if (uint8* const* Target = DataTable->GetRowMap().Find(Row.Key); Target && *Target)
		{
			RowStruct->CopyScriptStruct(*Target, Row.Value);
			DataTable->HandleDataTableChanged(Row.Key);
			Changed.Add(Row.Key);
			Result.Rows.Add(Row.Key.ToString());
		}
	}
	FDataTableEditorUtils::BroadcastPostChange(DataTable, FDataTableEditorUtils::EDataTableChangeInfo::RowData);

	Result.bApplied = true;
	FinishEdit(DataTable, Columns, Changed, Result);
	return Result;
}

FAgentMcpDataTableEditResult UAgentMcpDataTableTools::AddRows(UDataTable* DataTable, const TArray<FString>& RowNames, const FJsonObjectWrapper& Values)
{
	using namespace UE::AgentMcp::DataTableToolsPrivate;

	FAgentMcpDataTableEditResult Result;
	const UScriptStruct* RowStruct = RequireEditableTable(DataTable);
	if (!RowStruct)
	{
		return Result;
	}
	if (RowNames.IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'rowNames' must name at least one row."));
		return Result;
	}

	const TArray<FProperty*> Columns = GetColumns(RowStruct);
	FStagedRows Staged(RowStruct);
	FProblems Problems;
	TSet<FName> Seen;
	for (const FString& Requested : RowNames)
	{
		FName RowName;
		FString Problem;
		if (!MakeNewRowName(Requested, RowName, Problem))
		{
			Problems.Add(TEXT("INVALID_ARGUMENT"), Problem);
		}
		else if (DataTable->GetRowMap().Contains(RowName))
		{
			Problems.Add(TEXT("ROW_EXISTS"), FString::Printf(TEXT("row '%s' already exists"), *RowName.ToString()));
		}
		else if (Seen.Contains(RowName))
		{
			Problems.Add(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("row '%s' is listed twice"), *RowName.ToString()));
		}
		else
		{
			Seen.Add(RowName);
			Staged.Add(RowName, nullptr);
		}
	}
	if (Values.JsonObject.IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Values.JsonObject->Values)
		{
			if (uint8* RowMemory = Staged.Find(Pair.Key))
			{
				StageColumnValues(Columns, Pair.Key, Pair.Value, RowMemory, Problems);
			}
			else
			{
				Problems.Add(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("values are given for '%s', which is not a new row in rowNames"), *Pair.Key));
			}
		}
	}
	if (Problems.Raise(DataTable))
	{
		return Result;
	}

	TArray<FName> Added;
	for (const TPair<FName, uint8*>& Row : Staged.GetRows())
	{
		if (!FDataTableEditorUtils::AddRow(DataTable, Row.Key))
		{
			// Rows added before this one are rolled back with the dispatcher transaction.
			UE::AgentMcp::RaiseToolError(TEXT("ROW_ADD_FAILED"), FString::Printf(TEXT("Row '%s' could not be added to %s."), *Row.Key.ToString(), *DataTable->GetPathName()));
			return Result;
		}
		Added.Add(Row.Key);
	}

	// Copy the staged values over the default values of the new rows.
	FDataTableEditorUtils::BroadcastPreChange(DataTable, FDataTableEditorUtils::EDataTableChangeInfo::RowData);
	DataTable->Modify();
	for (const TPair<FName, uint8*>& Row : Staged.GetRows())
	{
		if (uint8* const* Target = DataTable->GetRowMap().Find(Row.Key); Target && *Target)
		{
			RowStruct->CopyScriptStruct(*Target, Row.Value);
			DataTable->HandleDataTableChanged(Row.Key);
		}
		Result.Rows.Add(Row.Key.ToString());
	}
	FDataTableEditorUtils::BroadcastPostChange(DataTable, FDataTableEditorUtils::EDataTableChangeInfo::RowData);

	Result.bApplied = true;
	FinishEdit(DataTable, Columns, Added, Result);
	return Result;
}

FAgentMcpDataTableEditResult UAgentMcpDataTableTools::RenameRows(UDataTable* DataTable, const TMap<FString, FString>& Renames)
{
	using namespace UE::AgentMcp::DataTableToolsPrivate;

	FAgentMcpDataTableEditResult Result;
	if (!RequireEditableTable(DataTable))
	{
		return Result;
	}
	if (Renames.IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'renames' must contain at least one row."));
		return Result;
	}

	FProblems Problems;
	TArray<TPair<FName, FName>> Pairs;
	TSet<FName> NewNames;
	for (const TPair<FString, FString>& Rename : Renames)
	{
		FName OldName;
		FName NewName;
		FString Problem;
		if (!FindRow(DataTable, Rename.Key, OldName))
		{
			Problems.Add(TEXT("UNKNOWN_ROW"), FString::Printf(TEXT("row '%s' does not exist"), *Rename.Key));
		}
		else if (!MakeNewRowName(Rename.Value, NewName, Problem))
		{
			Problems.Add(TEXT("INVALID_ARGUMENT"), Problem);
		}
		else if (DataTable->GetRowMap().Contains(NewName))
		{
			Problems.Add(TEXT("ROW_EXISTS"), FString::Printf(TEXT("row '%s' already exists; rename it away in a separate call first"), *NewName.ToString()));
		}
		else if (NewNames.Contains(NewName))
		{
			Problems.Add(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'%s' is used as a new name twice"), *NewName.ToString()));
		}
		else
		{
			NewNames.Add(NewName);
			Pairs.Emplace(OldName, NewName);
		}
	}
	if (Problems.Raise(DataTable))
	{
		return Result;
	}

	for (const TPair<FName, FName>& Pair : Pairs)
	{
		if (!FDataTableEditorUtils::RenameRow(DataTable, Pair.Key, Pair.Value))
		{
			UE::AgentMcp::RaiseToolError(TEXT("ROW_RENAME_FAILED"), FString::Printf(TEXT("Row '%s' could not be renamed to '%s'."), *Pair.Key.ToString(), *Pair.Value.ToString()));
			return Result;
		}
		Result.Rows.Add(Pair.Value.ToString());
	}

	Result.bApplied = true;
	FinishEdit(DataTable, TArray<FProperty*>(), TArray<FName>(), Result);
	return Result;
}

FAgentMcpDataTableEditResult UAgentMcpDataTableTools::RemoveRows(UDataTable* DataTable, const TArray<FString>& RowNames, bool bConfirm)
{
	using namespace UE::AgentMcp::DataTableToolsPrivate;

	FAgentMcpDataTableEditResult Result;
	if (!RequireEditableTable(DataTable))
	{
		return Result;
	}
	if (RowNames.IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'rowNames' must name at least one row."));
		return Result;
	}

	FProblems Problems;
	TArray<FName> Targets;
	for (const FString& Requested : RowNames)
	{
		FName RowName;
		if (FindRow(DataTable, Requested, RowName))
		{
			Targets.AddUnique(RowName);
		}
		else
		{
			Problems.Add(TEXT("UNKNOWN_ROW"), FString::Printf(TEXT("row '%s' does not exist"), *Requested));
		}
	}
	if (Problems.Raise(DataTable))
	{
		return Result;
	}

	if (bConfirm)
	{
		for (const FName RowName : Targets)
		{
			if (!FDataTableEditorUtils::RemoveRow(DataTable, RowName))
			{
				UE::AgentMcp::RaiseToolError(TEXT("ROW_REMOVE_FAILED"), FString::Printf(TEXT("Row '%s' could not be removed from %s."), *RowName.ToString(), *DataTable->GetPathName()));
				return Result;
			}
		}
		Result.bApplied = true;
	}
	else
	{
		Result.Warnings.Add(TEXT("Dry run: nothing was removed. Call again with bConfirm true to remove these rows."));
	}

	for (const FName RowName : Targets)
	{
		Result.Rows.Add(RowName.ToString());
	}
	FinishEdit(DataTable, TArray<FProperty*>(), TArray<FName>(), Result);
	return Result;
}
