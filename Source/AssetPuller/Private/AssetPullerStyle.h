#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/** Slate style set holding the plugin's toolbar/tab icon. */
class FAssetPullerStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static void ReloadTextures();
	static const ISlateStyle& Get();
	static FName GetStyleSetName();

private:
	static TSharedRef<class FSlateStyleSet> Create();
	static TSharedPtr<class FSlateStyleSet> StyleInstance;
};
