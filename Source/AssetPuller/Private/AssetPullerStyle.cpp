#include "AssetPullerStyle.h"

#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FAssetPullerStyle::StyleInstance = nullptr;

void FAssetPullerStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FAssetPullerStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FAssetPullerStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("AssetPullerStyle"));
	return StyleSetName;
}

TSharedRef<FSlateStyleSet> FAssetPullerStyle::Create()
{
	const FVector2D Icon20x20(20.0f, 20.0f);

	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("AssetPullerStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("AssetPuller")->GetBaseDir() / TEXT("Resources"));
	Style->Set("AssetPuller.OpenPluginWindow", new IMAGE_BRUSH(TEXT("Icon128"), Icon20x20));
	return Style;
}

void FAssetPullerStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FAssetPullerStyle::Get()
{
	return *StyleInstance;
}

#undef RootToContentDir
