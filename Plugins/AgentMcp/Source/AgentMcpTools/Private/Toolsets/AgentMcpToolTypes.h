#pragma once

#include "CoreMinimal.h"

#include "AgentMcpToolTypes.generated.h"

/** World that a scene tool reads. */
UENUM(BlueprintType)
enum class EAgentMcpWorld : uint8
{
	/** The level open in the editor. */
	Editor,
	/** The running play session (Play In Editor or Simulate). */
	Play,
};

USTRUCT(BlueprintType)
struct FAgentMcpPlaySessionState
{
	GENERATED_BODY()

	/** A play session is starting or running. */
	UPROPERTY()
	bool bActive = false;

	/** The session was requested but its world has not begun play yet. */
	UPROPERTY()
	bool bStarting = false;

	/** The play world has begun play. */
	UPROPERTY()
	bool bBegunPlay = false;

	/** Simulate In Editor rather than Play In Editor. */
	UPROPERTY()
	bool bSimulating = false;

	UPROPERTY()
	bool bPaused = false;

	/** Package of the play world, empty when not playing. */
	UPROPERTY()
	FString World;

	/** Game time of the play world in seconds. */
	UPROPERTY()
	double WorldTimeSeconds = 0.0;
};

/** C++ class and where it is declared. */
USTRUCT(BlueprintType)
struct FAgentMcpNativeClassInfo
{
	GENERATED_BODY()

	/** Class path, for example /Script/UMG.UserWidget. */
	UPROPERTY()
	FString ClassPath;

	/** Module package, for example /Script/UMG. */
	UPROPERTY()
	FString Module;

	/** Header relative to the module source folder (ModuleRelativePath metadata), for example Public/Blueprint/UserWidget.h. */
	UPROPERTY()
	FString Header;
};

/** Transform exchanged as arrays, so callers never pass a partial vector by accident. */
USTRUCT(BlueprintType)
struct FAgentMcpTransformValue
{
	GENERATED_BODY()

	/** [X, Y, Z] in centimeters. */
	UPROPERTY()
	TArray<double> Location;

	/** [Pitch, Yaw, Roll] in degrees. */
	UPROPERTY()
	TArray<double> Rotation;

	/** [X, Y, Z]. */
	UPROPERTY()
	TArray<double> Scale;
};
