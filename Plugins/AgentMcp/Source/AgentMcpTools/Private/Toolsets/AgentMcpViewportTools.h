#pragma once

#include "CoreMinimal.h"
#include "AgentMcpImage.h"
#include "AgentMcpToolset.h"

#include "AgentMcpViewportTools.generated.h"

USTRUCT(BlueprintType)
struct FAgentMcpViewportCapture
{
	GENERATED_BODY()

	/** Play for the play session viewport, Editor for the level editor viewport. */
	UPROPERTY()
	FString Source;

	/** The image includes the game UI (UMG widgets) drawn over the play session viewport. */
	UPROPERTY()
	bool bIncludesUI = false;

	/** Size of the returned image. */
	UPROPERTY()
	int32 Width = 0;

	UPROPERTY()
	int32 Height = 0;

	/** Size of the captured area before scaling. */
	UPROPERTY()
	int32 SourceWidth = 0;

	UPROPERTY()
	int32 SourceHeight = 0;

	/** Full path of the saved PNG file. */
	UPROPERTY()
	FString FilePath;

	/** Every pixel has the same color, which usually means the viewport was not rendered (for example a minimized window). */
	UPROPERTY()
	bool bUniformColor = false;

	/** The captured image; sent as MCP image content. */
	UPROPERTY()
	FAgentMcpImage Image;
};

USTRUCT(BlueprintType)
struct FAgentMcpViewportCameraResult
{
	GENERATED_BODY()

	/** Camera location [X, Y, Z] after the change. */
	UPROPERTY()
	TArray<double> Location;

	/** Camera rotation [Pitch, Yaw, Roll] after the change. */
	UPROPERTY()
	TArray<double> Rotation;

	/** Actor the camera was pointed at, when one was named. */
	UPROPERTY()
	FString FocusActor;
};

/** Viewport screenshots of the level editor or the play session, including the game UI. */
UCLASS(meta = (McpToolset = "viewport"))
class UAgentMcpViewportTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Captures a viewport as a PNG image: the play session viewport while a session runs, otherwise the level editor viewport.
	 * The image is returned as MCP image content and saved under Saved/MCP/Captures.
	 * @param MaxWidth Width to scale a wider image down to (64-4096); the aspect ratio is kept.
	 * @param bPreferPlay Capture the play session viewport when a session is running.
	 * @param bIncludeUI Include the game UI (UMG widgets) of the play session viewport, as the player sees it. The level editor viewport is captured without editor UI.
	 * @return Capture details and the image.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Viewport", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpViewportCapture Capture(int32 MaxWidth = 1024, bool bPreferPlay = true, bool bIncludeUI = true);

	/**
	 * Points the level editor viewport camera somewhere, so that the next capture shows it. Pass a location and rotation, or an actor to frame.
	 * @param Location Camera location [X, Y, Z] in centimeters.
	 * @param Rotation Camera rotation [Pitch, Yaw, Roll] in degrees.
	 * @param FocusActor Actor to frame, as pressing F in the viewport does. It overrides location and rotation.
	 * @return Where the camera ended up.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Viewport", meta = (AICallable, McpAccess = "Control", AutoCreateRefTerm = "Location,Rotation", BlueprintInternalUseOnly = "true"))
	static FAgentMcpViewportCameraResult SetCamera(const TArray<double>& Location, const TArray<double>& Rotation, AActor* FocusActor = nullptr);
};
