#include "AgentMcpAssetTools.h"

#include "AgentMcpToolsCommon.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace UE::AgentMcp::AssetToolsPrivate
{
	constexpr int32 MaxPageSize = 500;
	constexpr int32 MaxTags = 64;
	constexpr int32 MaxTagValueLength = 512;

	IAssetRegistry& GetAssetRegistry()
	{
		return FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	}

	FString GetClassTag(const FAssetData& AssetData, const TCHAR* TagName)
	{
		FString Value;
		AssetData.GetTagValue(FName(TagName), Value);
		return Tools::StripExportTextPath(Value);
	}

	FAgentMcpAssetSummary MakeSummary(const FAssetData& AssetData)
	{
		FAgentMcpAssetSummary Summary;
		Summary.Path = AssetData.GetObjectPathString();
		Summary.PackageName = AssetData.PackageName.ToString();
		Summary.Name = AssetData.AssetName.ToString();
		Summary.ClassName = AssetData.AssetClassPath.GetAssetName().ToString();
		Summary.ParentClass = GetClassTag(AssetData, TEXT("ParentClass"));
		Summary.NativeParentClass = GetClassTag(AssetData, TEXT("NativeParentClass"));
		return Summary;
	}

	/** Actors and objects saved in their own packages belong to levels; the actor tools address them. */
	bool IsExternalObjectPackage(const FName PackageName)
	{
		const FString Name = PackageName.ToString();
		return Name.Contains(TEXT("/__ExternalActors__/")) || Name.Contains(TEXT("/__ExternalObjects__/"));
	}

	/** Accepts an object path or a package name. Raises INVALID_ARGUMENT or NOT_FOUND. */
	bool ResolveAsset(const FString& Asset, FAssetData& OutAssetData, TArray<FString>* OutOtherAssets = nullptr)
	{
		const FString Text = Tools::StripExportTextPath(Asset);
		if (!Text.StartsWith(TEXT("/")) || Text.Len() >= NAME_SIZE)
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'asset' must be a content path such as /Game/Folder/Asset, not '%s'."), *Text.Left(128)));
			return false;
		}

		IAssetRegistry& AssetRegistry = GetAssetRegistry();
		const bool bIsObjectPath = Text.Contains(TEXT("."));
		if (bIsObjectPath)
		{
			OutAssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(Text));
			if (OutAssetData.IsValid())
			{
				return true;
			}
		}

		const FString PackageName = bIsObjectPath ? FPackageName::ObjectPathToPackageName(Text) : Text;
		TArray<FAssetData> PackageAssets;
		AssetRegistry.GetAssetsByPackageName(FName(*PackageName), PackageAssets);
		if (PackageAssets.Num() == 0)
		{
			RaiseToolError(TEXT("NOT_FOUND"), FString::Printf(TEXT("The asset registry has no asset '%s'."), *Text), TEXT("Use asset_find to look up asset paths."));
			return false;
		}

		const FString ShortName = FPackageName::GetShortName(PackageName);
		int32 MainIndex = PackageAssets.IndexOfByPredicate([&ShortName](const FAssetData& Candidate)
		{
			return Candidate.AssetName.ToString().Equals(ShortName, ESearchCase::IgnoreCase);
		});
		if (MainIndex == INDEX_NONE)
		{
			MainIndex = 0;
		}
		OutAssetData = PackageAssets[MainIndex];

		if (OutOtherAssets)
		{
			for (int32 Index = 0; Index < PackageAssets.Num(); ++Index)
			{
				if (Index != MainIndex)
				{
					OutOtherAssets->Add(PackageAssets[Index].GetObjectPathString());
				}
			}
		}
		return true;
	}

	bool CollectPackageReferences(const FString& Asset, bool bReferencers, bool bIncludeSoft, bool bIncludeScriptPackages, int32 Limit, int32 Cursor, FAgentMcpPackageReferenceResult& Result)
	{
		FAssetData AssetData;
		if (!ResolveAsset(Asset, AssetData))
		{
			return false;
		}

		IAssetRegistry& AssetRegistry = GetAssetRegistry();
		Result.PackageName = AssetData.PackageName.ToString();
		Result.bAssetRegistryLoading = AssetRegistry.IsLoadingAssets();

		TArray<FAssetDependency> Found;
		const FAssetIdentifier Identifier(AssetData.PackageName);
		if (bReferencers)
		{
			AssetRegistry.GetReferencers(Identifier, Found, UE::AssetRegistry::EDependencyCategory::Package);
		}
		else
		{
			AssetRegistry.GetDependencies(Identifier, Found, UE::AssetRegistry::EDependencyCategory::Package);
		}

		// Keep one entry per package and merge the dependency kinds.
		TMap<FName, FAgentMcpPackageReference> ByPackage;
		for (const FAssetDependency& Dependency : Found)
		{
			const FName PackageName = Dependency.AssetId.PackageName;
			if (PackageName.IsNone() || PackageName == AssetData.PackageName)
			{
				continue;
			}
			const bool bHard = EnumHasAnyFlags(Dependency.Properties, UE::AssetRegistry::EDependencyProperty::Hard);
			const bool bGame = EnumHasAnyFlags(Dependency.Properties, UE::AssetRegistry::EDependencyProperty::Game);
			if ((!bHard && !bIncludeSoft) || (!bIncludeScriptPackages && FPackageName::IsScriptPackage(PackageName.ToString())))
			{
				continue;
			}

			FAgentMcpPackageReference* Entry = ByPackage.Find(PackageName);
			if (!Entry)
			{
				Entry = &ByPackage.Add(PackageName);
				Entry->PackageName = PackageName.ToString();
				Entry->bEditorOnly = true;
			}
			Entry->bHard = Entry->bHard || bHard;
			Entry->bEditorOnly = Entry->bEditorOnly && !bGame;
		}

		TArray<FAgentMcpPackageReference> Packages;
		ByPackage.GenerateValueArray(Packages);
		Packages.Sort([](const FAgentMcpPackageReference& A, const FAgentMcpPackageReference& B)
		{
			return A.PackageName < B.PackageName;
		});

		int32 Start = 0;
		int32 End = 0;
		int32 NextCursor = -1;
		if (!Tools::GetPage(Packages.Num(), Limit, Cursor, MaxPageSize, Start, End, NextCursor))
		{
			return false;
		}
		Result.TotalMatched = Packages.Num();
		Result.NextCursor = NextCursor;

		for (int32 Index = Start; Index < End; ++Index)
		{
			FAgentMcpPackageReference& Entry = Result.Packages.Add_GetRef(Packages[Index]);
			TArray<FAssetData> PackageAssets;
			AssetRegistry.GetAssetsByPackageName(FName(*Entry.PackageName), PackageAssets);
			if (PackageAssets.Num() > 0)
			{
				Entry.ClassName = PackageAssets[0].AssetClassPath.GetAssetName().ToString();
			}
		}
		return true;
	}
}

FAgentMcpAssetFindResult UAgentMcpAssetTools::Find(const FString& Path, UClass* AssetClass, const FString& Name, bool bRecursive, int32 Limit, int32 Cursor)
{
	using namespace UE::AgentMcp::AssetToolsPrivate;

	FAgentMcpAssetFindResult Result;
	FString SearchPath = Path.TrimStartAndEnd();
	const FString NameFilter = Name.TrimStartAndEnd();
	if ((!SearchPath.IsEmpty() && !SearchPath.StartsWith(TEXT("/"))) || SearchPath.Len() >= NAME_SIZE)
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'path' must be a folder such as /Game/UI, not '%s'."), *SearchPath.Left(128)));
		return Result;
	}
	while (SearchPath.Len() > 1 && SearchPath.EndsWith(TEXT("/")))
	{
		SearchPath.LeftChopInline(1);
	}

	const bool bAllContent = SearchPath.IsEmpty() || SearchPath == TEXT("/");
	if (bAllContent && !AssetClass && NameFilter.IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("Searching all content needs assetClass or name."), TEXT("Pass a folder such as /Game, an assetClass or a name."));
		return Result;
	}

	FARFilter Filter;
	if (!bAllContent)
	{
		Filter.PackagePaths.Add(FName(*SearchPath));
	}
	Filter.bRecursivePaths = bRecursive;
	if (AssetClass)
	{
		Filter.ClassPaths.Add(AssetClass->GetClassPathName());
		Filter.bRecursiveClasses = true;
	}

	IAssetRegistry& AssetRegistry = GetAssetRegistry();
	Result.bAssetRegistryLoading = AssetRegistry.IsLoadingAssets();

	TArray<FAssetData> Found;
	AssetRegistry.GetAssets(Filter, Found);

	TArray<const FAssetData*> Matches;
	for (const FAssetData& AssetData : Found)
	{
		if (!IsExternalObjectPackage(AssetData.PackageName) && UE::AgentMcp::Tools::MatchesNameFilter(AssetData.AssetName.ToString(), NameFilter))
		{
			Matches.Add(&AssetData);
		}
	}
	Matches.Sort([](const FAssetData& A, const FAssetData& B)
	{
		if (A.PackageName != B.PackageName)
		{
			return A.PackageName.LexicalLess(B.PackageName);
		}
		return A.AssetName.LexicalLess(B.AssetName);
	});

	int32 Start = 0;
	int32 End = 0;
	int32 NextCursor = -1;
	if (!UE::AgentMcp::Tools::GetPage(Matches.Num(), Limit, Cursor, MaxPageSize, Start, End, NextCursor))
	{
		return Result;
	}
	Result.TotalMatched = Matches.Num();
	Result.NextCursor = NextCursor;
	for (int32 Index = Start; Index < End; ++Index)
	{
		Result.Assets.Add(MakeSummary(*Matches[Index]));
	}
	return Result;
}

FAgentMcpAssetDetails UAgentMcpAssetTools::Inspect(const FString& Asset)
{
	using namespace UE::AgentMcp::AssetToolsPrivate;

	FAgentMcpAssetDetails Details;
	FAssetData AssetData;
	if (!ResolveAsset(Asset, AssetData, &Details.OtherAssetsInPackage))
	{
		return Details;
	}

	IAssetRegistry& AssetRegistry = GetAssetRegistry();
	Details.Path = AssetData.GetObjectPathString();
	Details.PackageName = AssetData.PackageName.ToString();
	Details.PackagePath = AssetData.PackagePath.ToString();
	Details.Name = AssetData.AssetName.ToString();
	Details.ClassName = AssetData.AssetClassPath.GetAssetName().ToString();
	Details.ClassPath = AssetData.AssetClassPath.ToString();
	Details.bLoaded = AssetData.IsAssetLoaded();
	if (const UPackage* Package = FindPackage(nullptr, *Details.PackageName))
	{
		Details.bDirty = Package->IsDirty();
	}
	if (const TOptional<FAssetPackageData> PackageData = AssetRegistry.GetAssetPackageDataCopy(AssetData.PackageName); PackageData.IsSet())
	{
		Details.DiskSizeBytes = PackageData->DiskSize;
	}

	TArray<FName> PackageNames;
	AssetRegistry.GetDependencies(AssetData.PackageName, PackageNames);
	Details.DependencyCount = PackageNames.Num();
	PackageNames.Reset();
	AssetRegistry.GetReferencers(AssetData.PackageName, PackageNames);
	Details.ReferencerCount = PackageNames.Num();

	static const FName FindInBlueprintsTag(TEXT("FiBData"));
	AssetData.TagsAndValues.ForEach([&Details](const auto& Tag)
	{
		// Find-in-Blueprints search data is long and not useful to callers.
		if (Details.Tags.Num() >= MaxTags || Tag.Key == FindInBlueprintsTag)
		{
			return;
		}
		FString Value = Tag.Value.AsString();
		if (Value.Len() > MaxTagValueLength)
		{
			Value.LeftInline(MaxTagValueLength);
			Value += TEXT("...");
		}
		Details.Tags.Add(Tag.Key.ToString(), Value);
	});
	return Details;
}

FAgentMcpPackageReferenceResult UAgentMcpAssetTools::Referencers(const FString& Asset, bool bIncludeSoft, int32 Limit, int32 Cursor)
{
	FAgentMcpPackageReferenceResult Result;
	UE::AgentMcp::AssetToolsPrivate::CollectPackageReferences(Asset, /*bReferencers=*/true, bIncludeSoft, /*bIncludeScriptPackages=*/true, Limit, Cursor, Result);
	return Result;
}

FAgentMcpPackageReferenceResult UAgentMcpAssetTools::Dependencies(const FString& Asset, bool bIncludeSoft, bool bIncludeScriptPackages, int32 Limit, int32 Cursor)
{
	FAgentMcpPackageReferenceResult Result;
	UE::AgentMcp::AssetToolsPrivate::CollectPackageReferences(Asset, /*bReferencers=*/false, bIncludeSoft, bIncludeScriptPackages, Limit, Cursor, Result);
	return Result;
}

FAgentMcpSaveResult UAgentMcpAssetTools::Save(const TArray<FString>& Assets, bool bOnlyIfDirty, bool bAllowCheckout)
{
	FAgentMcpSaveResult Result;
	if (Assets.IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'assets' must name at least one asset."), TEXT("editor_get_state lists the dirty packages."));
		return Result;
	}
	if (GEditor && (GEditor->PlayWorld || GEditor->IsPlaySessionInProgress()))
	{
		UE::AgentMcp::RaiseToolError(TEXT("PIE_ACTIVE"), TEXT("Assets cannot be saved while a play session is running."), TEXT("Stop the play session first (pie_stop)."));
		return Result;
	}

	TArray<UPackage*> PackagesToSave;
	TArray<FString> Refusals;
	for (const FString& Asset : Assets)
	{
		const FString Text = UE::AgentMcp::Tools::StripExportTextPath(Asset);
		const FString PackageName = Text.Contains(TEXT(".")) ? FPackageName::ObjectPathToPackageName(Text) : Text;
		UPackage* Package = (PackageName.StartsWith(TEXT("/")) && PackageName.Len() < NAME_SIZE) ? FindPackage(nullptr, *PackageName) : nullptr;

		FString Refusal;
		if (!Package)
		{
			Refusal = TEXT("the package is not loaded, so it has no unsaved changes");
		}
		else if (Package->ContainsMap())
		{
			Refusal = TEXT("levels are saved from the editor, not with this tool");
		}
		else if (!UE::AgentMcp::Tools::IsProjectContentPackage(PackageName))
		{
			Refusal = TEXT("only project content (/Game and project plugins) can be saved");
		}
		else if (bOnlyIfDirty && !Package->IsDirty())
		{
			FAgentMcpSaveEntry& Entry = Result.Packages.AddDefaulted_GetRef();
			Entry.PackageName = PackageName;
			Entry.Status = TEXT("NotDirty");
			continue;
		}
		else
		{
			const TArray<FString> SourceControlWarnings = UE::AgentMcp::Tools::GetSourceControlWarnings(Package);
			if (!SourceControlWarnings.IsEmpty() && !bAllowCheckout)
			{
				Refusal = FString::Join(SourceControlWarnings, TEXT("; ")) + TEXT(" Pass bAllowCheckout true to check it out.");
			}
			else
			{
				Result.Warnings.Append(SourceControlWarnings);
				PackagesToSave.AddUnique(Package);
				continue;
			}
		}

		FAgentMcpSaveEntry& Entry = Result.Packages.AddDefaulted_GetRef();
		Entry.PackageName = PackageName;
		Entry.Status = TEXT("Refused");
		Entry.Reason = Refusal;
		Refusals.Add(FString::Printf(TEXT("%s: %s"), *PackageName, *Refusal));
	}

	TArray<FString> Failures;
	if (!PackagesToSave.IsEmpty())
	{
		// Without a dialog, SavePackages suppresses modal windows (GIsRunningUnattendedScript) and checks out files under source control.
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, bOnlyIfDirty);

		for (const UPackage* Package : PackagesToSave)
		{
			FAgentMcpSaveEntry& Entry = Result.Packages.AddDefaulted_GetRef();
			Entry.PackageName = Package->GetName();
			const FString Filename = FPackageName::LongPackageNameToFilename(Entry.PackageName, FPackageName::GetAssetPackageExtension());
			if (!Package->IsDirty() && IFileManager::Get().FileExists(*Filename))
			{
				Entry.Status = TEXT("Saved");
				++Result.SavedCount;
			}
			else
			{
				Entry.Status = TEXT("Failed");
				Entry.Reason = TEXT("the package still has unsaved changes; log_get_recent with categories LogSavePackage,LogFileHelpers shows why");
				Failures.Add(FString::Printf(TEXT("%s: %s"), *Entry.PackageName, *Entry.Reason));
			}
		}
	}

	if (Result.SavedCount == 0 && (!Refusals.IsEmpty() || !Failures.IsEmpty()))
	{
		TArray<FString> Messages = Failures;
		Messages.Append(Refusals);
		UE::AgentMcp::RaiseToolError(Failures.IsEmpty() ? TEXT("SAVE_REFUSED") : TEXT("SAVE_FAILED"),
			FString::Printf(TEXT("Nothing was saved: %s"), *FString::Join(Messages, TEXT("; "))));
	}
	return Result;
}
