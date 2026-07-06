#include "AssetPullerSettings.h"

#include "Misc/Paths.h"

UAssetPullerSettings::UAssetPullerSettings()
{
	// No default: every machine keeps its own library location. Set it once in
	// Project Settings > Plugins > Asset Puller or in the Asset Puller window;
	// it persists in the project's Config/DefaultEditor.ini.
	SourceContentDir.Path = TEXT("");
}

FString UAssetPullerSettings::GetSourceContentDir() const
{
	FString Dir = SourceContentDir.Path;
	FPaths::NormalizeDirectoryName(Dir);
	return Dir;
}

void UAssetPullerSettings::SetSourceContentDir(const FString& InDir)
{
	FString Dir = InDir;
	FPaths::NormalizeDirectoryName(Dir);
	if (SourceContentDir.Path != Dir)
	{
		SourceContentDir.Path = Dir;
		SaveToConfig();
	}
}

void UAssetPullerSettings::SetIncludeSoftReferences(bool bInclude)
{
	if (bIncludeSoftReferences != bInclude)
	{
		bIncludeSoftReferences = bInclude;
		SaveToConfig();
	}
}

void UAssetPullerSettings::SaveToConfig()
{
	// defaultconfig classes write to Config/DefaultEditor.ini via this call.
	TryUpdateDefaultConfigFile();
}
