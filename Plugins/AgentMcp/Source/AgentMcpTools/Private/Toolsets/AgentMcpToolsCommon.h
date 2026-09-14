#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolTypes.h"

class UClass;
class UObject;
class UWorld;

/** Helpers shared by the concrete toolsets. Functions that fail raise a tool error and return false or null. */
namespace UE::AgentMcp::Tools
{
	/** The editor level world or the play session world. Raises EDITOR_UNAVAILABLE or PIE_NOT_ACTIVE when it does not exist. */
	UWorld* ResolveWorld(EAgentMcpWorld World);

	/** "Editor" for the editor level, "Play" for a play session world. */
	FString GetWorldKind(const UWorld* World);

	FAgentMcpPlaySessionState MakePlaySessionState();

	FAgentMcpTransformValue MakeTransformValue(const FTransform& Transform);

	/**
	 * Reads an optional [X, Y, Z] argument; an empty array means the argument was omitted (bOutProvided is false).
	 * Raises INVALID_ARGUMENT unless the array is empty or holds exactly three finite numbers.
	 */
	bool ReadOptionalVector(const TArray<double>& Values, const TCHAR* ArgumentName, bool& bOutProvided, FVector& OutVector);

	/** Raises INVALID_ARGUMENT when Object is null. */
	bool RequireObject(const UObject* Object, const TCHAR* ArgumentName);

	/**
	 * Computes the page [OutStart, OutEnd) of Total items. Limit is clamped to 1..MaxLimit and OutNextCursor is -1 on the
	 * last page. Raises INVALID_ARGUMENT for a cursor outside 0..Total.
	 */
	bool GetPage(int32 Total, int32 Limit, int32 Cursor, int32 MaxLimit, int32& OutStart, int32& OutEnd, int32& OutNextCursor);

	/** Case-insensitive substring match, or wildcard match when the pattern contains * or ?. An empty pattern matches everything. */
	bool MatchesNameFilter(const FString& Value, const FString& Pattern);

	/** Class'/Script/Engine.Actor' (export text used by asset registry tags) becomes /Script/Engine.Actor. */
	FString StripExportTextPath(const FString& Text);

	/** First C++ class in the hierarchy, starting with the class itself. */
	const UClass* FindNativeClass(const UClass* Class);

	FAgentMcpNativeClassInfo MakeNativeClassInfo(const UClass* NativeClass);

	/** First line of a tooltip, at most MaxLength characters. */
	FString GetShortDescription(const FString& ToolTip, int32 MaxLength = 200);

	/** Package under /Game or in a project plugin. */
	bool IsProjectContentPackage(const FString& PackageName);

	/** Assets under /Game or in project plugins. Engine and engine plugin content is read-only for tools. Raises NOT_SUPPORTED. */
	bool RequireProjectContent(const UObject* Asset);

	/** Source control warnings for saving a package, from the cached state (no source control request). */
	TArray<FString> GetSourceControlWarnings(const UPackage* Package);
}
