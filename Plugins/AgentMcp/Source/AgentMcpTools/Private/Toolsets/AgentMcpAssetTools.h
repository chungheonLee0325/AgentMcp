#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"
#include "JsonObjectWrapper.h"

#include "AgentMcpAssetTools.generated.h"

USTRUCT(BlueprintType)
struct FAgentMcpAssetSummary
{
	GENERATED_BODY()

	/** Object path; pass it to tools that take the asset. */
	UPROPERTY()
	FString Path;

	UPROPERTY()
	FString PackageName;

	UPROPERTY()
	FString Name;

	/** Asset class name, for example WidgetBlueprint, Blueprint or DataTable. */
	UPROPERTY()
	FString ClassName;

	/** Parent class of a Blueprint asset. */
	UPROPERTY()
	FString ParentClass;

	/** First C++ parent class of a Blueprint asset. */
	UPROPERTY()
	FString NativeParentClass;
};

USTRUCT(BlueprintType)
struct FAgentMcpAssetFindResult
{
	GENERATED_BODY()

	/** Matching assets on this page, sorted by package name. */
	UPROPERTY()
	TArray<FAgentMcpAssetSummary> Assets;

	/** Number of matching assets on all pages. */
	UPROPERTY()
	int32 TotalMatched = 0;

	/** Cursor of the next page, -1 on the last page. */
	UPROPERTY()
	int32 NextCursor = -1;

	/** The asset registry is still scanning, so results may be incomplete. */
	UPROPERTY()
	bool bAssetRegistryLoading = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpAssetDetails
{
	GENERATED_BODY()

	/** Object path. */
	UPROPERTY()
	FString Path;

	UPROPERTY()
	FString PackageName;

	/** Folder of the package. */
	UPROPERTY()
	FString PackagePath;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString ClassName;

	UPROPERTY()
	FString ClassPath;

	/** The asset is loaded in the editor. */
	UPROPERTY()
	bool bLoaded = false;

	/** The package is loaded and has unsaved changes. */
	UPROPERTY()
	bool bDirty = false;

	/** Package file size in bytes, -1 when unknown. */
	UPROPERTY()
	int64 DiskSizeBytes = -1;

	/** Packages this asset depends on, including C++ module packages. */
	UPROPERTY()
	int32 DependencyCount = 0;

	/** Packages that reference this asset. */
	UPROPERTY()
	int32 ReferencerCount = 0;

	/** Asset registry tags; long values are cut. */
	UPROPERTY()
	TMap<FString, FString> Tags;

	/** Other assets stored in the same package. */
	UPROPERTY()
	TArray<FString> OtherAssetsInPackage;
};

USTRUCT(BlueprintType)
struct FAgentMcpPackageReference
{
	GENERATED_BODY()

	UPROPERTY()
	FString PackageName;

	/** Class of the main asset in the package; empty for C++ module packages. */
	UPROPERTY()
	FString ClassName;

	/** Loaded together with the asset. False for soft references. */
	UPROPERTY()
	bool bHard = false;

	/** Only the editor needs the reference. */
	UPROPERTY()
	bool bEditorOnly = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpPackageReferenceResult
{
	GENERATED_BODY()

	/** Package that was queried. */
	UPROPERTY()
	FString PackageName;

	/** Referencing or referenced packages on this page, sorted by name. */
	UPROPERTY()
	TArray<FAgentMcpPackageReference> Packages;

	UPROPERTY()
	int32 TotalMatched = 0;

	/** Cursor of the next page, -1 on the last page. */
	UPROPERTY()
	int32 NextCursor = -1;

	/** The asset registry is still scanning, so results may be incomplete. */
	UPROPERTY()
	bool bAssetRegistryLoading = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpSaveEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString PackageName;

	/** Saved, NotDirty, Refused or Failed. */
	UPROPERTY()
	FString Status;

	/** Why the package was refused or failed. */
	UPROPERTY()
	FString Reason;
};

USTRUCT(BlueprintType)
struct FAgentMcpSaveResult
{
	GENERATED_BODY()

	/** Result per requested package. */
	UPROPERTY()
	TArray<FAgentMcpSaveEntry> Packages;

	UPROPERTY()
	int32 SavedCount = 0;

	/** Source control notes for packages that were checked out to be saved. */
	UPROPERTY()
	TArray<FString> Warnings;
};

USTRUCT(BlueprintType)
struct FAgentMcpAssetCreateResult
{
	GENERATED_BODY()

	/** Object path of the new asset. */
	UPROPERTY()
	FString Asset;

	UPROPERTY()
	FString ClassName;

	/** Row struct of a new DataTable. */
	UPROPERTY()
	FString RowStruct;
};

USTRUCT(BlueprintType)
struct FAgentMcpImportedTexture
{
	GENERATED_BODY()

	/** Object path of the texture. */
	UPROPERTY()
	FString Asset;

	/** Absolute path of the imported file. */
	UPROPERTY()
	FString File;

	UPROPERTY()
	int32 Width = 0;

	UPROPERTY()
	int32 Height = 0;

	/** An existing texture was replaced. */
	UPROPERTY()
	bool bReplaced = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpTextureImportResult
{
	GENERATED_BODY()

	/** Imported textures in the order of the entries. */
	UPROPERTY()
	TArray<FAgentMcpImportedTexture> Textures;
};

/** Asset registry queries, saving, creating data assets and importing textures. The query tools do not load assets. */
UCLASS(meta = (McpToolset = "asset"))
class UAgentMcpAssetTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Finds assets by folder, class and name without loading them. Results are sorted by package name.
	 * @param Path Folder to search, for example /Game/UI. / searches all mounted content and then needs assetClass or name.
	 * @param AssetClass Only assets of this class or a subclass, for example WidgetBlueprint, Blueprint or DataTable.
	 * @param Name Case-insensitive text contained in the asset name; * and ? act as wildcards.
	 * @param bRecursive Include subfolders.
	 * @param Limit Maximum number of assets to return (1-500).
	 * @param Cursor nextCursor of the previous call; 0 for the first page.
	 * @return One page of assets with their object paths.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Asset", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpAssetFindResult Find(const FString& Path = TEXT("/Game"), UClass* AssetClass = nullptr, const FString& Name = TEXT(""), bool bRecursive = true, int32 Limit = 50, int32 Cursor = 0);

	/**
	 * Describes an asset from the asset registry: class, tags, file size, loaded and dirty state, reference counts. Does not load the asset.
	 * @param Asset Object path (/Game/UI/WBP_Main.WBP_Main) or package name (/Game/UI/WBP_Main).
	 * @return Asset description.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Asset", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpAssetDetails Inspect(const FString& Asset);

	/**
	 * Lists packages that reference an asset, for example the Blueprints that use a DataTable.
	 * @param Asset Object path or package name.
	 * @param bIncludeSoft Include soft references.
	 * @param Limit Maximum number of packages to return (1-500).
	 * @param Cursor nextCursor of the previous call; 0 for the first page.
	 * @return One page of referencing packages.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Asset", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpPackageReferenceResult Referencers(const FString& Asset, bool bIncludeSoft = true, int32 Limit = 100, int32 Cursor = 0);

	/**
	 * Lists packages that an asset depends on.
	 * @param Asset Object path or package name.
	 * @param bIncludeSoft Include soft references.
	 * @param bIncludeScriptPackages Include C++ module packages (/Script/...).
	 * @param Limit Maximum number of packages to return (1-500).
	 * @param Cursor nextCursor of the previous call; 0 for the first page.
	 * @return One page of packages the asset depends on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Asset", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpPackageReferenceResult Dependencies(const FString& Asset, bool bIncludeSoft = true, bool bIncludeScriptPackages = false, int32 Limit = 100, int32 Cursor = 0);

	/**
	 * Saves loaded assets to disk without dialogs. Only project content can be saved and levels are not saved by this tool.
	 * Packages under source control must already be checked out unless bAllowCheckout is true. Saving cannot be undone.
	 * @param Assets Object paths or package names of the assets to save.
	 * @param bOnlyIfDirty Skip packages without unsaved changes.
	 * @param bAllowCheckout Check out packages under source control that are not checked out yet.
	 * @return Result per package.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Asset", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpSaveResult Save(const TArray<FString>& Assets, bool bOnlyIfDirty = true, bool bAllowCheckout = false);

	/**
	 * Creates a data asset (an instance of a DataAsset subclass, for example a UI theme or an item definition) or a DataTable under /Game
	 * or in a project plugin. Set its values afterwards with object_set_properties or the datatable tools. The asset is not saved (use
	 * asset_save), its creation cannot be undone with editor_undo, and the tool is blocked during a play session.
	 * @param AssetPath Package path of the new asset, for example /Game/UI/DA_Theme. No asset may exist there yet.
	 * @param AssetClass DataAsset subclass to create, or DataTable.
	 * @param RowStruct Row struct of a DataTable: a path such as /Script/MyGame.ItemRow, or the struct name. Leave it empty for data assets.
	 * @return The new asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Asset", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpAssetCreateResult Create(const FString& AssetPath, UClass* AssetClass, const FString& RowStruct = TEXT(""));

	/**
	 * Imports image files (PNG, JPEG, TGA, BMP) as textures under /Game or in a project plugin, without dialogs. Every entry is checked
	 * before anything is imported. With bUserInterface the textures get the settings for UMG: texture group UI, no mipmaps and
	 * UserInterface2D compression. The textures are not saved (use asset_save), importing cannot be undone with editor_undo, and the tool
	 * is blocked during a play session.
	 * @param Textures Entries {"file": image file, "asset": package path of the texture}; a relative file starts at the project folder. For example [{"file": "Art/Incoming/icon_core.png", "asset": "/Game/UI/Textures/T_Icon_Core"}].
	 * @param bReplaceExisting Replace textures that exist at the asset paths; without it an existing asset is an error.
	 * @param bUserInterface Apply the texture settings for UMG.
	 * @return The imported textures with their size.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Asset", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpTextureImportResult ImportTextures(const TArray<FJsonObjectWrapper>& Textures, bool bReplaceExisting = false, bool bUserInterface = true);
};
