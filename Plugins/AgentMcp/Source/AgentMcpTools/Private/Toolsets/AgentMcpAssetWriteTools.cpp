#include "AgentMcpAssetTools.h"

#include "AgentMcpToolsCommon.h"

#include "AssetImportTask.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/DataAssetFactory.h"
#include "Factories/DataTableFactory.h"
#include "Factories/StringTableFactory.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "Internationalization/StringTable.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodySetup.h"
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

	bool IsMeshFile(const FString& File)
	{
		const FString Extension = FPaths::GetExtension(File);
		for (const TCHAR* Supported : { TEXT("fbx"), TEXT("gltf"), TEXT("glb"), TEXT("obj") })
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

	/**
	 * Checks import entries {"file", "asset"} before anything is imported, because imports cannot be undone. Raises every problem in one
	 * error and returns false. An existing asset is only replaced when it is a ReplaceableClass and bReplaceExisting is set.
	 */
	bool PlanImports(const TArray<FJsonObjectWrapper>& Entries, const TCHAR* ArgumentName, bool bReplaceExisting, bool (*IsSupportedFile)(const FString&),
		const TCHAR* FileKinds, const UClass* ReplaceableClass, const FString& EntryHint, TArray<FPlannedImport>& OutPlanned)
	{
		if (Entries.IsEmpty() || Entries.Num() > MaxImportEntries)
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'%s' must list 1-%d entries, not %d."), ArgumentName, MaxImportEntries, Entries.Num()), EntryHint);
			return false;
		}

		const IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FString> Problems;
		TSet<FString> ProblemCodes;
		TSet<FString> PlannedPackages;
		auto AddProblem = [&Problems, &ProblemCodes](const FString& Message, const FString& Code)
		{
			Problems.Add(Message);
			ProblemCodes.Add(Code);
		};

		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const FString Label = FString::Printf(TEXT("%s[%d]"), ArgumentName, Index);
			const TSharedPtr<FJsonObject>& Entry = Entries[Index].JsonObject;
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
			if (!IsSupportedFile(Plan.File))
			{
				AddProblem(FString::Printf(TEXT("%s: %s is not a %s file"), *Label, *Plan.File, FileKinds), TEXT("INVALID_ARGUMENT"));
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
					AddProblem(FString::Printf(TEXT("%s: an asset already exists at %s; pass bReplaceExisting true to replace it"), *Label, *Plan.PackageName), TEXT("ASSET_EXISTS"));
					continue;
				}
				TArray<FAssetData> Existing;
				AssetRegistry.GetAssetsByPackageName(FName(*Plan.PackageName), Existing);
				const bool bReplaceable = Existing.ContainsByPredicate([ReplaceableClass](const FAssetData& AssetData)
				{
					return AssetData.AssetClassPath == ReplaceableClass->GetClassPathName();
				});
				if (!bReplaceable)
				{
					AddProblem(FString::Printf(TEXT("%s: the asset at %s is not a %s, so it is not replaced"), *Label, *Plan.PackageName, *ReplaceableClass->GetName()), TEXT("ASSET_EXISTS"));
					continue;
				}
				Plan.bReplace = true;
			}
			OutPlanned.Add(MoveTemp(Plan));
		}
		if (!Problems.IsEmpty())
		{
			Tools::RaiseProblems(TEXT("Nothing was imported"), Problems, ProblemCodes, EntryHint);
			return false;
		}
		return true;
	}

	/** Runs one import task synchronously and without dialogs; the task keeps the created objects. */
	void RunImportTask(UAssetImportTask* Task, const FPlannedImport& Plan)
	{
		Task->Filename = Plan.File;
		Task->DestinationPath = FPackageName::GetLongPackagePath(Plan.PackageName);
		Task->DestinationName = Plan.AssetName;
		Task->bReplaceExisting = Plan.bReplace;
		Task->bReplaceExistingSettings = false;
		Task->bAutomated = true;
		Task->bSave = false;
		Task->bAsync = false;
		GetAssetTools().ImportAssetTasks({ Task });
	}
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
	const bool bStringTable = AssetClass == UStringTable::StaticClass();
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
	else if (bStringTable)
	{
		if (!RowStructName.IsEmpty())
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'rowStruct' only applies to DataTables."), TEXT("Leave rowStruct empty for string tables."));
			return Result;
		}
	}
	else if (!AssetClass->IsChildOf(UDataAsset::StaticClass()))
	{
		RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("asset_create creates data assets, DataTables and string tables, not %s assets."), *AssetClass->GetName()),
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
	else if (bStringTable)
	{
		Factory = NewObject<UStringTableFactory>();
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

FAgentMcpTextureImportResult UAgentMcpAssetTools::ImportTextures(const TArray<FJsonObjectWrapper>& Textures, bool bReplaceExisting, bool bUserInterface, bool bMipmaps)
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
	TArray<FPlannedImport> Planned;
	if (!PlanImports(Textures, TEXT("textures"), bReplaceExisting, &IsImageFile, TEXT("PNG, JPEG, TGA or BMP"), UTexture2D::StaticClass(), EntryHint, Planned))
	{
		return Result;
	}

	for (const FPlannedImport& Plan : Planned)
	{
		TStrongObjectPtr<UAssetImportTask> Task(NewObject<UAssetImportTask>());
		RunImportTask(Task.Get(), Plan);

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
			Texture->MipGenSettings = bMipmaps ? TMGS_SimpleAverage : TMGS_NoMipmaps;
			Texture->CompressionSettings = TC_EditorIcon;
			Texture->SRGB = true;
			if (bMipmaps)
			{
				// Slate samples with the texture's Filter, and the UI group's default does not blend mips (MipFilter=point in BaseDeviceProfiles.ini).
				Texture->Filter = TF_Trilinear;
				Texture->NeverStream = true;
			}
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

FAgentMcpMeshImportResult UAgentMcpAssetTools::ImportMeshes(const TArray<FJsonObjectWrapper>& Meshes, bool bReplaceExisting)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::AssetWriteToolsPrivate;

	FAgentMcpMeshImportResult Result;
	if (Tools::IsPlaySessionActive())
	{
		RaiseToolError(TEXT("PIE_ACTIVE"), TEXT("asset_import_meshes creates assets and is blocked while a play session is running."),
			TEXT("Stop the play session first (pie_stop)."));
		return Result;
	}
	const FString EntryHint = TEXT("Each entry is {\"file\": mesh file, \"asset\": package path}, for example {\"file\": \"Art/Kit/wall.gltf\", \"asset\": \"/Game/Kit/SM_Wall\"}.");
	TArray<FPlannedImport> Planned;
	if (!PlanImports(Meshes, TEXT("meshes"), bReplaceExisting, &IsMeshFile, TEXT("FBX, glTF, GLB or OBJ"), UStaticMesh::StaticClass(), EntryHint, Planned))
	{
		return Result;
	}

	for (const FPlannedImport& Plan : Planned)
	{
		TStrongObjectPtr<UAssetImportTask> Task(NewObject<UAssetImportTask>());
		RunImportTask(Task.Get(), Plan);

		// One file can bring several objects: its meshes, and the materials and textures they refer to.
		TArray<UStaticMesh*> FileMeshes;
		TArray<FString> Created;
		for (UObject* Object : Task->GetObjects())
		{
			if (!Object)
			{
				continue;
			}
			if (UStaticMesh* Candidate = Cast<UStaticMesh>(Object))
			{
				FileMeshes.Add(Candidate);
				continue;
			}
			Object->MarkPackageDirty();
			Created.Add(Object->GetPathName());
		}
		if (FileMeshes.IsEmpty())
		{
			TArray<FString> ImportedBefore;
			for (const FAgentMcpImportedMesh& Done : Result.Meshes)
			{
				ImportedBefore.Add(Done.Asset);
			}
			const FString Before = ImportedBefore.IsEmpty() ? FString() : FString::Printf(TEXT(" Imported before the failure: %s."), *FString::Join(ImportedBefore, TEXT(", ")));
			RaiseToolError(TEXT("IMPORT_FAILED"), FString::Printf(TEXT("%s did not produce a static mesh at %s.%s"), *Plan.File, *Plan.PackageName, *Before),
				TEXT("A skeletal mesh or an empty file produces no static mesh; log_get_recent may show the reason."));
			return Result;
		}
		// A file of several parts (a chest and its lid, a doorway and its door) gets one mesh per part, named after the parts, and
		// the parts share their origin, so placing them on the same transform puts the piece back together. None of them is "the"
		// mesh of the file, so every part is reported.
		if (FileMeshes.Num() > 1)
		{
			TArray<FString> Parts;
			for (const UStaticMesh* Part : FileMeshes)
			{
				Parts.Add(Part->GetPackage()->GetName());
			}
			Result.Warnings.Add(FString::Printf(TEXT("%s holds %d meshes, so each part was imported under its own name instead of %s: %s."),
				*Plan.File, FileMeshes.Num(), *Plan.PackageName, *FString::Join(Parts, TEXT(", "))));
		}
		else if (!FileMeshes[0]->GetPackage()->GetName().Equals(Plan.PackageName, ESearchCase::IgnoreCase))
		{
			Result.Warnings.Add(FString::Printf(TEXT("%s was imported as %s, not %s."), *Plan.File, *FileMeshes[0]->GetPackage()->GetName(), *Plan.PackageName));
		}

		for (int32 Index = 0; Index < FileMeshes.Num(); ++Index)
		{
			UStaticMesh* Mesh = FileMeshes[Index];
			Mesh->MarkPackageDirty();
			FAgentMcpImportedMesh& Info = Result.Meshes.AddDefaulted_GetRef();
			Info.Asset = Mesh->GetPathName();
			Info.File = Plan.File;
			const FVector Size = Mesh->GetBoundingBox().GetSize();
			Info.Size = { Size.X, Size.Y, Size.Z };
			const UBodySetup* BodySetup = Mesh->GetBodySetup();
			Info.CollisionShapes = BodySetup ? BodySetup->AggGeom.GetElementCount() : 0;
			Info.bReplaced = Plan.bReplace && Mesh->GetPackage()->GetName().Equals(Plan.PackageName, ESearchCase::IgnoreCase);
			if (Index == 0)
			{
				Info.CreatedAssets = Created;
			}
		}
	}
	return Result;
}
