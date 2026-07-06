#pragma once

#include "AssetPullerStyle.h"
#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class FAssetPullerCommands : public TCommands<FAssetPullerCommands>
{
public:
	FAssetPullerCommands()
		: TCommands<FAssetPullerCommands>(TEXT("AssetPuller"),
			NSLOCTEXT("Contexts", "AssetPuller", "Asset Puller Plugin"),
			NAME_None, FAssetPullerStyle::GetStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> OpenPluginWindow;
};
