#include "AgentMcpClassTools.h"

#include "AgentMcpToolsCommon.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace UE::AgentMcp::ClassToolsPrivate
{
	constexpr int32 MaxPageSize = 500;

	/** Classes left behind by Blueprint compilation and hot reload. */
	bool IsTransientClassName(const FString& ClassName)
	{
		return ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_"))
			|| ClassName.StartsWith(TEXT("TRASHCLASS_")) || ClassName.StartsWith(TEXT("HOTRELOADED_"));
	}
}

FAgentMcpDerivedClassResult UAgentMcpClassTools::FindDerived(UClass* BaseClass, const FString& NameContains, bool bIncludeNative, bool bIncludeBlueprint, int32 Limit, int32 Cursor)
{
	using namespace UE::AgentMcp::ClassToolsPrivate;

	FAgentMcpDerivedClassResult Result;
	if (!UE::AgentMcp::Tools::RequireObject(BaseClass, TEXT("baseClass")))
	{
		return Result;
	}
	Result.BaseClass = BaseClass->GetPathName();

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Result.bAssetRegistryLoading = AssetRegistry.IsLoadingAssets();

	TMap<FString, FAgentMcpDerivedClass> ByPath;

	// Loaded classes: every C++ class and the Blueprint classes that are loaded.
	TArray<UClass*> LoadedClasses;
	GetDerivedClasses(BaseClass, LoadedClasses, /*bRecursive=*/true);
	for (const UClass* Class : LoadedClasses)
	{
		if (!Class || Class->HasAnyClassFlags(CLASS_NewerVersionExists) || IsTransientClassName(Class->GetName()))
		{
			continue;
		}
		const bool bNative = Class->HasAnyClassFlags(CLASS_Native);
		if ((bNative && !bIncludeNative) || (!bNative && !bIncludeBlueprint))
		{
			continue;
		}

		FAgentMcpDerivedClass& Entry = ByPath.FindOrAdd(Class->GetPathName());
		Entry.ClassPath = Class->GetPathName();
		Entry.Name = Class->GetName();
		Entry.bNative = bNative;
		Entry.bLoaded = true;
		if (bNative)
		{
			Entry.Module = Class->GetPackage()->GetName();
			Entry.Header = Class->GetMetaData(TEXT("ModuleRelativePath"));
		}
		else if (const UObject* GeneratedBy = Class->ClassGeneratedBy)
		{
			Entry.Blueprint = GeneratedBy->GetPathName();
		}
	}

	// Blueprint classes known to the asset registry, including Blueprints that are not loaded.
	if (bIncludeBlueprint)
	{
		const FTopLevelAssetPath BasePath = BaseClass->GetClassPathName();
		TSet<FTopLevelAssetPath> DerivedPaths;
		AssetRegistry.GetDerivedClassNames(TArray<FTopLevelAssetPath>{ BasePath }, TSet<FTopLevelAssetPath>(), DerivedPaths);
		for (const FTopLevelAssetPath& DerivedPath : DerivedPaths)
		{
			const FString ClassPath = DerivedPath.ToString();
			const FString ClassName = DerivedPath.GetAssetName().ToString();
			if (DerivedPath == BasePath || ByPath.Contains(ClassPath) || IsTransientClassName(ClassName)
				|| FPackageName::IsScriptPackage(DerivedPath.GetPackageName().ToString()))
			{
				continue;
			}

			FAgentMcpDerivedClass& Entry = ByPath.Add(ClassPath);
			Entry.ClassPath = ClassPath;
			Entry.Name = ClassName;
			// Blueprint generated classes are named <Asset>_C inside the Blueprint package.
			FString AssetName = ClassName;
			AssetName.RemoveFromEnd(TEXT("_C"));
			Entry.Blueprint = FString::Printf(TEXT("%s.%s"), *DerivedPath.GetPackageName().ToString(), *AssetName);
		}
	}

	const FString NameFilter = NameContains.TrimStartAndEnd();
	TArray<FAgentMcpDerivedClass> Matches;
	for (TPair<FString, FAgentMcpDerivedClass>& Pair : ByPath)
	{
		if (UE::AgentMcp::Tools::MatchesNameFilter(Pair.Value.Name, NameFilter))
		{
			Matches.Add(MoveTemp(Pair.Value));
		}
	}
	Matches.Sort([](const FAgentMcpDerivedClass& A, const FAgentMcpDerivedClass& B)
	{
		return A.ClassPath < B.ClassPath;
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
		Result.Classes.Add(MoveTemp(Matches[Index]));
	}
	return Result;
}
