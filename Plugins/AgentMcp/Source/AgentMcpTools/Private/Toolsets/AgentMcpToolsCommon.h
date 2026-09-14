#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolTypes.h"

class FJsonObject;
class FJsonValue;
class FProperty;
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

	/** A play session is starting or running. */
	bool IsPlaySessionActive();

	/**
	 * Checks the path of an asset to create: an object path (/Game/UI/T_Icon.T_Icon) or a package path (/Game/UI/T_Icon) of project
	 * content with a valid asset name. Returns an empty string and sets OutPackageName and OutAssetName, or returns the problem without a
	 * final period and sets OutCode to INVALID_ARGUMENT or NOT_SUPPORTED. Does not check whether an asset exists.
	 */
	FString GetNewAssetPathProblem(const FString& AssetPath, FString& OutPackageName, FString& OutAssetName, FString& OutCode);

	/** An asset exists at the package path: loaded, on disk or in the asset registry. */
	bool DoesAssetExist(const FString& PackageName, const FString& AssetName);

	/** Source control warnings for saving a package, from the cached state (no source control request). */
	TArray<FString> GetSourceControlWarnings(const UPackage* Package);

	/** False for sparse class data, whose values are not stored in the object. */
	bool IsPropertyStoredOnObject(const FProperty* Property, const UObject* Object);

	/** A property of the object's class by name, or null. The name is looked up without adding it to the name table. */
	FProperty* FindPropertyByName(const UObject* Object, const FString& PropertyName);

	/**
	 * Empty when tools may change the property on the object; otherwise the reason. bCheckAsInstance checks a class default object as if
	 * it were an instance of its class, for values that are checked before the instance exists.
	 */
	FString GetPropertyNotEditableReason(const UObject* Object, const FProperty* Property, bool bCheckAsInstance = false);

	/** The property value as JSON, or a JSON null when the value cannot be represented. */
	TSharedRef<FJsonValue> ReadPropertyValue(const UObject* Object, const FProperty* Property);

	/** Property values for one object, converted from JSON and checked, so that a bad value is found before anything changes. */
	class FPreparedPropertyValues : public FNoncopyable
	{
	public:
		struct FEntry
		{
			FProperty* Property = nullptr;

			/** Initialized value of the property's type. */
			void* Value = nullptr;
		};

		~FPreparedPropertyValues();

		/**
		 * Checks and converts each value for Object, starting from the object's current value so that struct fields missing from the JSON
		 * keep their value. Appends each problem, without a final period, and its error code. Returns true when there were no problems.
		 */
		bool Prepare(const UObject* Object, const FJsonObject& Values, TArray<FString>& OutProblems, TSet<FString>& OutProblemCodes, bool bCheckAsInstance = false);

		const TArray<FEntry>& GetEntries() const
		{
			return Entries;
		}

	private:
		TArray<FEntry> Entries;
	};

	/** Raises one error for collected problems, "<Summary>: <problem>; <problem>.", with their error code, or INVALID_ARGUMENT when the codes differ. */
	void RaiseProblems(const FString& Summary, const TArray<FString>& Problems, const TSet<FString>& ProblemCodes, const FString& Hint = FString());
}
