#include "AgentMcpLevelTools.h"

#include "AgentMcpToolsCommon.h"

#include "Editor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "LevelEditorSubsystem.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "WorldPartition/WorldPartition.h"

namespace UE::AgentMcp::LevelToolsPrivate
{
	/** Levels are replaced as a whole, so the check covers the map package and the external actor packages of a partitioned level. */
	bool IsLevelPackage(const UPackage* Package)
	{
		return Package->ContainsMap() || Package->GetName().Contains(TEXT("/__External"));
	}

	/**
	 * Dirty level packages. An untitled world under /Temp and content outside the project are left out, because no tool here can save
	 * them: counting them would block every level_new and level_open with no way out.
	 */
	TArray<FString> CollectUnsavedLevels()
	{
		TArray<FString> Names;
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			const UPackage* Package = *It;
			if (!Package || !Package->IsDirty() || Package == GetTransientPackage() || Package->HasAnyPackageFlags(PKG_CompiledIn) || !IsLevelPackage(Package))
			{
				continue;
			}
			const FString PackageName = Package->GetName();
			if (PackageName.StartsWith(TEXT("/Script/")) || PackageName.StartsWith(TEXT("/Temp/")) || !Tools::IsProjectContentPackage(PackageName))
			{
				continue;
			}
			Names.Add(PackageName);
		}
		Names.Sort();
		return Names;
	}

	/** Loaded packages of the levels to save, with the built data package each level keeps its lighting in. */
	TArray<UPackage*> FindPackagesToSave(const TArray<FString>& PackageNames)
	{
		TArray<UPackage*> Packages;
		for (const FString& PackageName : PackageNames)
		{
			if (UPackage* Package = FindPackage(nullptr, *PackageName))
			{
				Packages.AddUnique(Package);
			}
			if (UPackage* BuiltData = FindPackage(nullptr, *(PackageName + TEXT("_BuiltData"))); BuiltData && BuiltData->IsDirty())
			{
				Packages.AddUnique(BuiltData);
			}
		}
		return Packages;
	}

	ULevelEditorSubsystem* RequireSubsystem(const TCHAR* ToolName)
	{
		if (Tools::IsPlaySessionActive())
		{
			RaiseToolError(TEXT("PIE_ACTIVE"), FString::Printf(TEXT("%s changes the level open in the editor and is blocked while a play session is running."), ToolName),
				TEXT("Stop the play session first (pie_stop)."));
			return nullptr;
		}
		ULevelEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
		if (!Subsystem)
		{
			RaiseToolError(TEXT("EDITOR_UNAVAILABLE"), TEXT("The level editor subsystem is not available."));
		}
		return Subsystem;
	}

	/** Opening or creating a level discards unsaved level changes without asking, so they are reported instead of lost. */
	bool RequireSavedLevels(const TCHAR* ToolName)
	{
		const TArray<FString> Unsaved = CollectUnsavedLevels();
		if (Unsaved.IsEmpty())
		{
			return true;
		}
		RaiseToolError(TEXT("UNSAVED_CHANGES"),
			FString::Printf(TEXT("%s replaces the level in the editor, which would discard unsaved changes in %s."), ToolName, *FString::Join(Unsaved, TEXT(", "))),
			TEXT("Save them with level_save, or undo them with editor_undo."));
		return false;
	}

	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	void DescribeCurrentLevel(FAgentMcpLevelResult& Result)
	{
		if (const UWorld* World = GetEditorWorld())
		{
			Result.Level = World->GetPackage()->GetName();
			Result.bPartitioned = World->GetWorldPartition() != nullptr;
		}
		Result.UnsavedLevels = CollectUnsavedLevels();
	}

	/** A level package path that exists on disk, or an empty string with the problem raised. */
	FString ResolveExistingLevel(const FString& AssetPath, const TCHAR* ArgumentName)
	{
		FString PackageName = Tools::StripExportTextPath(AssetPath).TrimStartAndEnd();
		if (PackageName.Contains(TEXT(".")))
		{
			PackageName = FPackageName::ObjectPathToPackageName(PackageName);
		}
		if (PackageName.IsEmpty() || !PackageName.StartsWith(TEXT("/")) || PackageName.Len() >= NAME_SIZE)
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'%s' must be a package path, for example /Game/Maps/Greybox."), ArgumentName));
			return FString();
		}
		if (!FPackageName::DoesPackageExist(PackageName))
		{
			RaiseToolError(TEXT("NOT_FOUND"), FString::Printf(TEXT("No package exists at %s."), *PackageName),
				TEXT("asset_find with assetClass World lists the levels."));
			return FString();
		}
		return PackageName;
	}
}

FAgentMcpLevelResult UAgentMcpLevelTools::New(const FString& AssetPath, bool bPartitioned, const FString& TemplatePath)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::LevelToolsPrivate;

	FAgentMcpLevelResult Result;
	ULevelEditorSubsystem* Subsystem = RequireSubsystem(TEXT("level_new"));
	if (!Subsystem || !RequireSavedLevels(TEXT("level_new")))
	{
		return Result;
	}

	FString PackageName;
	FString AssetName;
	FString Code;
	const FString PathProblem = Tools::GetNewAssetPathProblem(AssetPath, PackageName, AssetName, Code);
	if (!PathProblem.IsEmpty())
	{
		RaiseToolError(Code, PathProblem + TEXT("."), TEXT("Pass a package path under /Game or a project plugin, for example /Game/Maps/Greybox."));
		return Result;
	}
	if (Tools::DoesAssetExist(PackageName, AssetName) || FPackageName::DoesPackageExist(PackageName))
	{
		RaiseToolError(TEXT("ASSET_EXISTS"), FString::Printf(TEXT("An asset already exists at %s."), *PackageName),
			TEXT("Choose another path, or open the level with level_open."));
		return Result;
	}

	FString Template = TemplatePath.TrimStartAndEnd();
	if (!Template.IsEmpty())
	{
		Template = ResolveExistingLevel(Template, TEXT("templatePath"));
		if (Template.IsEmpty())
		{
			return Result;
		}
	}

	// NewLevel and NewLevelFromTemplate save the level themselves; that is the one place a level reaches disk without level_save.
	const bool bCreated = Template.IsEmpty() ? Subsystem->NewLevel(PackageName, bPartitioned) : Subsystem->NewLevelFromTemplate(PackageName, Template);
	if (!bCreated)
	{
		RaiseToolError(TEXT("LEVEL_CREATE_FAILED"), FString::Printf(TEXT("%s could not be created."), *PackageName), TEXT("log_get_recent may show the reason."));
		return Result;
	}

	DescribeCurrentLevel(Result);
	Result.Template = Template;
	return Result;
}

FAgentMcpLevelResult UAgentMcpLevelTools::Open(const FString& AssetPath)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::LevelToolsPrivate;

	FAgentMcpLevelResult Result;
	ULevelEditorSubsystem* Subsystem = RequireSubsystem(TEXT("level_open"));
	if (!Subsystem || !RequireSavedLevels(TEXT("level_open")))
	{
		return Result;
	}

	const FString PackageName = ResolveExistingLevel(AssetPath, TEXT("assetPath"));
	if (PackageName.IsEmpty())
	{
		return Result;
	}
	if (const UWorld* World = GetEditorWorld(); World && World->GetPackage()->GetName().Equals(PackageName, ESearchCase::IgnoreCase))
	{
		DescribeCurrentLevel(Result);
		return Result;
	}

	if (!Subsystem->LoadLevel(PackageName))
	{
		RaiseToolError(TEXT("LEVEL_OPEN_FAILED"), FString::Printf(TEXT("%s could not be opened."), *PackageName),
			TEXT("It may not be a level; log_get_recent may show the reason."));
		return Result;
	}

	DescribeCurrentLevel(Result);
	return Result;
}

FAgentMcpLevelSaveResult UAgentMcpLevelTools::Save(bool bAllDirty)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::LevelToolsPrivate;

	FAgentMcpLevelSaveResult Result;
	ULevelEditorSubsystem* Subsystem = RequireSubsystem(TEXT("level_save"));
	if (!Subsystem)
	{
		return Result;
	}
	const UWorld* World = GetEditorWorld();
	if (!World)
	{
		RaiseToolError(TEXT("EDITOR_UNAVAILABLE"), TEXT("No level is open in the editor."));
		return Result;
	}
	const FString CurrentPackage = World->GetPackage()->GetName();
	// With bAllDirty the current level is only one of the candidates, and the list already holds project content only.
	if (!bAllDirty && !Tools::IsProjectContentPackage(CurrentPackage))
	{
		RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("%s is not project content, so it is not saved."), *CurrentPackage),
			TEXT("Only levels under /Game or a project plugin can be saved. level_new copies a template into project content."));
		return Result;
	}

	const TArray<FString> Before = CollectUnsavedLevels();
	if (Before.IsEmpty())
	{
		Result.Warnings.Add(TEXT("No level had unsaved changes."));
		return Result;
	}

	// SaveAllDirtyLevels would also write levels outside the project, so every level is saved by package instead.
	const TArray<UPackage*> Packages = bAllDirty ? FindPackagesToSave(Before) : TArray<UPackage*>();
	const bool bSaved = bAllDirty ? UEditorLoadingAndSavingUtils::SavePackages(Packages, /*bOnlyDirty=*/true) : Subsystem->SaveCurrentLevel();
	Result.UnsavedLevels = CollectUnsavedLevels();
	for (const FString& PackageName : Before)
	{
		if (!Result.UnsavedLevels.Contains(PackageName))
		{
			Result.Levels.Add(PackageName);
		}
	}
	if (!bSaved && Result.Levels.IsEmpty())
	{
		RaiseToolError(TEXT("LEVEL_SAVE_FAILED"), FString::Printf(TEXT("%s could not be saved."), *CurrentPackage), TEXT("log_get_recent may show the reason."));
		return Result;
	}
	if (!Result.UnsavedLevels.IsEmpty() && !bAllDirty)
	{
		Result.Warnings.Add(TEXT("Other levels still have unsaved changes; pass bAllDirty true to save them as well."));
	}
	return Result;
}
