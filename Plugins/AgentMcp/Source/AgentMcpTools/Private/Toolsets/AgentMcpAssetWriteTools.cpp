#include "AgentMcpAssetTools.h"

#include "AgentMcpToolsCommon.h"

#include "AssetImportTask.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Factories/DataAssetFactory.h"
#include "Factories/DataTableFactory.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"

namespace UE::AgentMcp::AssetWriteToolsPrivate
{
	constexpr int32 MaxImportEntries = 100;

	IAssetTools& GetAssetTools()
	{
		return FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	}

	/** A row struct by path (/Script/Module.Struct or a user-defined struct asset) or by name, with or without the C++ prefix F. */
	UScriptStruct* ResolveRowStruct(const FString& NameOrPath)
	{
		const FString Text = Tools::StripExportTextPath(NameOrPath);
		if (Text.IsEmpty() || Text.Len() >= NAME_SIZE)
		{
			return nullptr;
		}
		if (Text.StartsWith(TEXT("/")))
		{
			FString ObjectPath = Text;
			if (!ObjectPath.Contains(TEXT(".")))
			{
				ObjectPath += TEXT(".") + FPackageName::GetShortName(ObjectPath);
			}
			if (UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *ObjectPath))
			{
				return Struct;
			}
			return LoadObject<UScriptStruct>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		UScriptStruct* Struct = FindFirstObject<UScriptStruct>(*Text, EFindFirstObjectOptions::NativeFirst);
		if (!Struct && Text.Len() > 1 && Text[0] == TEXT('F'))
		{
			Struct = FindFirstObject<UScriptStruct>(*Text.RightChop(1), EFindFirstObjectOptions::NativeFirst);
		}
		return Struct;
	}

	/** Absolute path of a file argument; a relative path starts at the project folder. */
	FString MakeAbsoluteFile(const FString& File)
	{
		FString Path = File.TrimStartAndEnd();
		if (FPaths::IsRelative(Path))
		{
			Path = FPaths::Combine(FPaths::ProjectDir(), Path);
		}
		Path = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Path);
		return Path;
	}

	bool IsImageFile(const FString& File)
	{
		const FString Extension = FPaths::GetExtension(File);
		for (const TCHAR* Supported : { TEXT("png"), TEXT("jpg"), TEXT("jpeg"), TEXT("tga"), TEXT("bmp") })
		{
			if (Extension.Equals(Supported, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	struct FPlannedImport
	{
		FString File;
		FString PackageName;
		FString AssetName;
		bool bReplace = false;
	};
}

FAgentMcpAssetCreateResult UAgentMcpAssetTools::Create(const FString& AssetPath, UClass* AssetClass, const FString& RowStruct)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::AssetWriteToolsPrivate;

	FAgentMcpAssetCreateResult Result;
	// Control tools are not blocked during PIE by the dispatcher, but new assets belong to the editor session.
	if (Tools::IsPlaySessionActive())
	{
		RaiseToolError(TEXT("PIE_ACTIVE"), TEXT("asset_create creates an asset and is blocked while a play session is running."),
			TEXT("Stop the play session first (pie_stop)."));
		return Result;
	}
	if (!Tools::RequireObject(AssetClass, TEXT("assetClass")))
	{
		return Result;
	}

	FString PackageName;
	FString AssetName;
	FString Code;
	const FString PathProblem = Tools::GetNewAssetPathProblem(AssetPath, PackageName, AssetName, Code);
	if (!PathProblem.IsEmpty())
	{
		RaiseToolError(Code, PathProblem + TEXT("."), TEXT("Pass a package path under /Game or a project plugin, for example /Game/UI/DA_Theme."));
		return Result;
	}

	const bool bDataTable = AssetClass == UDataTable::StaticClass();
	const FString RowStructName = RowStruct.TrimStartAndEnd();
	UScriptStruct* Struct = nullptr;
	if (bDataTable)
	{
		if (RowStructName.IsEmpty())
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'rowStruct' is required to create a DataTable."),
				TEXT("Pass the row struct, for example /Script/MyGame.ItemRow or ItemRow."));
			return Result;
		}
		Struct = ResolveRowStruct(RowStructName);
		if (!Struct || !Struct->IsChildOf(FTableRowBase::StaticStruct()))
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'%s' is not a row struct, a struct derived from FTableRowBase."), *RowStructName.Left(256)),
				TEXT("Pass the struct path, for example /Script/MyGame.ItemRow, or its name without the F prefix."));
			return Result;
		}
	}
	else if (!AssetClass->IsChildOf(UDataAsset::StaticClass()))
	{
		RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("asset_create creates data assets and DataTables, not %s assets."), *AssetClass->GetName()),
			TEXT("Create Widget Blueprints with umg_create_widget_blueprint and textures with asset_import_textures."));
		return Result;
	}
	else if (AssetClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("%s is abstract or deprecated, so no asset can be created from it."), *AssetClass->GetName()),
			TEXT("class_find_derived on DataAsset lists the data asset classes."));
		return Result;
	}
	else if (!RowStructName.IsEmpty())
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'rowStruct' only applies to DataTables."), TEXT("Leave rowStruct empty for data assets."));
		return Result;
	}

	// IAssetTools::CreateAsset opens an overwrite dialog for an existing asset, so check first.
	if (Tools::DoesAssetExist(PackageName, AssetName))
	{
		RaiseToolError(TEXT("ASSET_EXISTS"), FString::Printf(TEXT("An asset already exists at %s."), *PackageName),
			TEXT("Choose another path, or change the existing asset with object_set_properties."));
		return Result;
	}

	UFactory* Factory = nullptr;
	if (bDataTable)
	{
		UDataTableFactory* DataTableFactory = NewObject<UDataTableFactory>();
		DataTableFactory->Struct = Struct;
		Factory = DataTableFactory;
	}
	else
	{
		UDataAssetFactory* DataAssetFactory = NewObject<UDataAssetFactory>();
		DataAssetFactory->DataAssetClass = AssetClass;
		Factory = DataAssetFactory;
	}

	UObject* Asset = GetAssetTools().CreateAsset(AssetName, FPackageName::GetLongPackagePath(PackageName), AssetClass, Factory);
	if (!Asset)
	{
		RaiseToolError(TEXT("ASSET_CREATE_FAILED"), FString::Printf(TEXT("%s could not be created."), *PackageName), TEXT("log_get_recent may show the reason."));
		return Result;
	}

	Result.Asset = Asset->GetPathName();
	Result.ClassName = Asset->GetClass()->GetName();
	if (Struct)
	{
		Result.RowStruct = Struct->GetPathName();
	}
	return Result;
}

FAgentMcpTextureImportResult UAgentMcpAssetTools::ImportTextures(const TArray<FJsonObjectWrapper>& Textures, bool bReplaceExisting, bool bUserInterface)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::AssetWriteToolsPrivate;

	FAgentMcpTextureImportResult Result;
	if (Tools::IsPlaySessionActive())
	{
		RaiseToolError(TEXT("PIE_ACTIVE"), TEXT("asset_import_textures creates assets and is blocked while a play session is running."),
			TEXT("Stop the play session first (pie_stop)."));
		return Result;
	}
	const FString EntryHint = TEXT("Each entry is {\"file\": image file, \"asset\": package path}, for example {\"file\": \"Art/Incoming/icon.png\", \"asset\": \"/Game/UI/Textures/T_Icon\"}.");
	if (Textures.IsEmpty() || Textures.Num() > MaxImportEntries)
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'textures' must list 1-%d entries, not %d."), MaxImportEntries, Textures.Num()), EntryHint);
		return Result;
	}

	// Check every entry before importing anything, because imports cannot be undone.
	const IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FPlannedImport> Planned;
	TArray<FString> Problems;
	TSet<FString> ProblemCodes;
	TSet<FString> PlannedPackages;
	auto AddProblem = [&Problems, &ProblemCodes](const FString& Message, const FString& Code)
	{
		Problems.Add(Message);
		ProblemCodes.Add(Code);
	};

	for (int32 Index = 0; Index < Textures.Num(); ++Index)
	{
		const FString Label = FString::Printf(TEXT("textures[%d]"), Index);
		const TSharedPtr<FJsonObject>& Entry = Textures[Index].JsonObject;
		if (!Entry.IsValid())
		{
			AddProblem(FString::Printf(TEXT("%s is not an object"), *Label), TEXT("INVALID_ARGUMENT"));
			continue;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Entry->Values)
		{
			if (!Pair.Key.Equals(TEXT("file"), ESearchCase::CaseSensitive) && !Pair.Key.Equals(TEXT("asset"), ESearchCase::CaseSensitive))
			{
				AddProblem(FString::Printf(TEXT("%s has the unknown field '%s' (expected file, asset)"), *Label, *Pair.Key.Left(64)), TEXT("INVALID_ARGUMENT"));
			}
		}
		FString File;
		FString Asset;
		Entry->TryGetStringField(TEXT("file"), File);
		Entry->TryGetStringField(TEXT("asset"), Asset);
		if (File.TrimStartAndEnd().IsEmpty() || Asset.TrimStartAndEnd().IsEmpty())
		{
			AddProblem(FString::Printf(TEXT("%s needs the strings file and asset"), *Label), TEXT("INVALID_ARGUMENT"));
			continue;
		}

		FPlannedImport Plan;
		Plan.File = MakeAbsoluteFile(File);
		FString Code;
		const FString PathProblem = Tools::GetNewAssetPathProblem(Asset, Plan.PackageName, Plan.AssetName, Code);
		if (!PathProblem.IsEmpty())
		{
			AddProblem(FString::Printf(TEXT("%s: %s"), *Label, *PathProblem), Code);
			continue;
		}
		if (!IsImageFile(Plan.File))
		{
			AddProblem(FString::Printf(TEXT("%s: %s is not a PNG, JPEG, TGA or BMP file"), *Label, *Plan.File), TEXT("INVALID_ARGUMENT"));
			continue;
		}
		if (!IFileManager::Get().FileExists(*Plan.File))
		{
			AddProblem(FString::Printf(TEXT("%s: the file %s does not exist"), *Label, *Plan.File), TEXT("NOT_FOUND"));
			continue;
		}
		if (PlannedPackages.Contains(Plan.PackageName))
		{
			AddProblem(FString::Printf(TEXT("%s: another entry already imports to %s"), *Label, *Plan.PackageName), TEXT("INVALID_ARGUMENT"));
			continue;
		}
		PlannedPackages.Add(Plan.PackageName);

		if (Tools::DoesAssetExist(Plan.PackageName, Plan.AssetName))
		{
			if (!bReplaceExisting)
			{
				AddProblem(FString::Printf(TEXT("%s: an asset already exists at %s; pass bReplaceExisting true to replace a texture"), *Label, *Plan.PackageName), TEXT("ASSET_EXISTS"));
				continue;
			}
			TArray<FAssetData> Existing;
			AssetRegistry.GetAssetsByPackageName(FName(*Plan.PackageName), Existing);
			const bool bTexture = Existing.ContainsByPredicate([](const FAssetData& AssetData)
			{
				return AssetData.AssetClassPath == UTexture2D::StaticClass()->GetClassPathName();
			});
			if (!bTexture)
			{
				AddProblem(FString::Printf(TEXT("%s: the asset at %s is not a texture, so it is not replaced"), *Label, *Plan.PackageName), TEXT("ASSET_EXISTS"));
				continue;
			}
			Plan.bReplace = true;
		}
		Planned.Add(MoveTemp(Plan));
	}
	if (!Problems.IsEmpty())
	{
		Tools::RaiseProblems(TEXT("Nothing was imported"), Problems, ProblemCodes, EntryHint);
		return Result;
	}

	for (const FPlannedImport& Plan : Planned)
	{
		TStrongObjectPtr<UAssetImportTask> Task(NewObject<UAssetImportTask>());
		Task->Filename = Plan.File;
		Task->DestinationPath = FPackageName::GetLongPackagePath(Plan.PackageName);
		Task->DestinationName = Plan.AssetName;
		Task->bReplaceExisting = Plan.bReplace;
		Task->bReplaceExistingSettings = false;
		Task->bAutomated = true;
		Task->bSave = false;
		Task->bAsync = false;
		GetAssetTools().ImportAssetTasks({ Task.Get() });

		UTexture2D* Texture = nullptr;
		for (UObject* Object : Task->GetObjects())
		{
			Texture = Cast<UTexture2D>(Object);
			if (Texture)
			{
				break;
			}
		}
		if (!Texture || !Texture->GetPackage()->GetName().Equals(Plan.PackageName, ESearchCase::IgnoreCase))
		{
			TArray<FString> ImportedBefore;
			for (const FAgentMcpImportedTexture& Done : Result.Textures)
			{
				ImportedBefore.Add(Done.Asset);
			}
			const FString Before = ImportedBefore.IsEmpty() ? FString() : FString::Printf(TEXT(" Imported before the failure: %s."), *FString::Join(ImportedBefore, TEXT(", ")));
			RaiseToolError(TEXT("IMPORT_FAILED"), FString::Printf(TEXT("%s could not be imported to %s.%s"), *Plan.File, *Plan.PackageName, *Before),
				TEXT("log_get_recent may show the reason."));
			return Result;
		}

		if (bUserInterface)
		{
			Texture->Modify();
			Texture->LODGroup = TEXTUREGROUP_UI;
			Texture->MipGenSettings = TMGS_NoMipmaps;
			Texture->CompressionSettings = TC_EditorIcon;
			Texture->SRGB = true;
			Texture->PostEditChange();
		}
		Texture->MarkPackageDirty();

		FAgentMcpImportedTexture& Info = Result.Textures.AddDefaulted_GetRef();
		Info.Asset = Texture->GetPathName();
		Info.File = Plan.File;
		Info.Width = static_cast<int32>(Texture->Source.GetSizeX());
		Info.Height = static_cast<int32>(Texture->Source.GetSizeY());
		Info.bReplaced = Plan.bReplace;
	}
	return Result;
}
