#include "AssetPullerCommands.h"

#define LOCTEXT_NAMESPACE "FAssetPullerModule"

void FAssetPullerCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "Asset Puller", "Pull assets from the source asset library", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
