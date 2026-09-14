#include "AgentMcpViewportTools.h"

#include "Editor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "LevelEditorViewport.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"

namespace UE::AgentMcp::ViewportToolsPrivate
{
	constexpr int32 MinCaptureWidth = 64;
	constexpr int32 MaxCaptureWidth = 4096;

	/**
	 * Reads the area of the play viewport widget from its window, including the game UI. Slate draws UMG over the scene render target,
	 * so GetViewportScreenShot does not contain it. UGameViewportClient::ProcessScreenShots also uses TakeScreenshot for screenshots with UI.
	 */
	bool CaptureWidgetWithUI(const TSharedPtr<SViewport>& ViewportWidget, TArray<FColor>& OutPixels, FIntPoint& OutSize)
	{
		if (!ViewportWidget.IsValid() || !FSlateApplication::IsInitialized())
		{
			return false;
		}

		FIntVector CaptureSize;
		if (!FSlateApplication::Get().TakeScreenshot(ViewportWidget.ToSharedRef(), OutPixels, CaptureSize)
			|| CaptureSize.X <= 0 || CaptureSize.Y <= 0 || OutPixels.Num() < CaptureSize.X * CaptureSize.Y)
		{
			return false;
		}
		OutPixels.SetNum(CaptureSize.X * CaptureSize.Y);
		OutSize = FIntPoint(CaptureSize.X, CaptureSize.Y);
		return true;
	}
}

FAgentMcpViewportCapture UAgentMcpViewportTools::Capture(int32 MaxWidth, bool bPreferPlay, bool bIncludeUI)
{
	using namespace UE::AgentMcp::ViewportToolsPrivate;

	FAgentMcpViewportCapture Result;
	if (!GEditor)
	{
		UE::AgentMcp::RaiseToolError(TEXT("EDITOR_UNAVAILABLE"), TEXT("GEditor is not available."));
		return Result;
	}

	FViewport* Viewport = nullptr;
	TSharedPtr<SViewport> PlayViewportWidget;
	if (bPreferPlay && GEditor->PlayWorld)
	{
		Viewport = GEditor->GetPIEViewport();
		if (UGameViewportClient* GameViewport = GEditor->PlayWorld->GetGameViewport())
		{
			PlayViewportWidget = GameViewport->GetGameViewportWidget();
		}
		Result.Source = TEXT("Play");
	}
	if (!Viewport)
	{
		// The level viewport the user worked in last; any active viewport otherwise.
		Viewport = (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->Viewport)
			? GCurrentLevelEditingViewportClient->Viewport
			: GEditor->GetActiveViewport();
		PlayViewportWidget.Reset();
		Result.Source = TEXT("Editor");
	}
	if (!Viewport)
	{
		UE::AgentMcp::RaiseToolError(TEXT("NOT_AVAILABLE"), TEXT("No viewport is available to capture."), TEXT("Open a level viewport, or start a play session with pie_start."));
		return Result;
	}

	FIntPoint SourceSize = Viewport->GetSizeXY();
	if (SourceSize.X <= 0 || SourceSize.Y <= 0)
	{
		UE::AgentMcp::RaiseToolError(TEXT("NOT_AVAILABLE"), TEXT("The viewport has no size."), TEXT("The editor window may be minimized."));
		return Result;
	}

	TArray<FColor> Pixels;
	if (bIncludeUI && PlayViewportWidget.IsValid() && CaptureWidgetWithUI(PlayViewportWidget, Pixels, SourceSize))
	{
		Result.bIncludesUI = true;
	}
	else
	{
		Pixels.Reset();
		if (!GetViewportScreenShot(Viewport, Pixels) || Pixels.Num() != SourceSize.X * SourceSize.Y)
		{
			UE::AgentMcp::RaiseToolError(TEXT("CAPTURE_FAILED"), TEXT("The viewport pixels could not be read."));
			return Result;
		}
	}

	// Alpha in the read pixels is not meaningful for a screenshot. Make every pixel opaque so the PNG shows what is on screen.
	bool bUniformColor = true;
	const FColor FirstPixel = Pixels[0];
	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255;
		bUniformColor = bUniformColor && Pixel.R == FirstPixel.R && Pixel.G == FirstPixel.G && Pixel.B == FirstPixel.B;
	}

	int32 Width = SourceSize.X;
	int32 Height = SourceSize.Y;
	const int32 TargetWidth = FMath::Clamp(MaxWidth, MinCaptureWidth, MaxCaptureWidth);
	if (Width > TargetWidth)
	{
		const int32 TargetHeight = FMath::Max(1, FMath::RoundToInt(static_cast<double>(Height) * TargetWidth / Width));
		TArray<FColor> Scaled;
		FImageUtils::ImageResize(Width, Height, Pixels, TargetWidth, TargetHeight, Scaled, /*bLinearSpace=*/false);
		Pixels = MoveTemp(Scaled);
		Width = TargetWidth;
		Height = TargetHeight;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> PngWriter = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!PngWriter.IsValid() || !PngWriter->SetRaw(Pixels.GetData(), static_cast<int64>(Pixels.Num()) * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
	{
		UE::AgentMcp::RaiseToolError(TEXT("CAPTURE_FAILED"), TEXT("The capture could not be encoded as PNG."));
		return Result;
	}
	const TArray64<uint8> Png = PngWriter->GetCompressed();

	const FString Directory = FPaths::ProjectSavedDir() / TEXT("MCP") / TEXT("Captures");
	const FString FilePath = FPaths::ConvertRelativePathToFull(Directory / FString::Printf(TEXT("%s_%s.png"), *Result.Source, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S_%s"))));
	IFileManager::Get().MakeDirectory(*Directory, /*Tree=*/true);
	if (FFileHelper::SaveArrayToFile(Png, *FilePath))
	{
		Result.FilePath = FilePath;
	}

	Result.Width = Width;
	Result.Height = Height;
	Result.SourceWidth = SourceSize.X;
	Result.SourceHeight = SourceSize.Y;
	Result.bUniformColor = bUniformColor;
	Result.Image.MimeType = TEXT("image/png");
	Result.Image.Width = Width;
	Result.Image.Height = Height;
	Result.Image.Data.Append(Png.GetData(), static_cast<int32>(Png.Num()));
	return Result;
}
