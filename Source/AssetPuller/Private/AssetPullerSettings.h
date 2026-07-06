#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AssetPullerSettings.generated.h"

/**
 * Project Settings -> Plugins -> Asset Puller.
 * Stored in the project's DefaultEditor.ini so the source path survives editor restarts.
 */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "Asset Puller"))
class UAssetPullerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAssetPullerSettings();

	//~ UDeveloperSettings
	virtual FName GetContainerName() const override { return FName("Project"); }
	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Content folder of the asset dump project to pull from (its "Content" directory on disk). */
	UPROPERTY(EditAnywhere, config, Category = "Source", meta = (DisplayName = "Source Content Folder"))
	FDirectoryPath SourceContentDir;

	/** Also follow soft references (recommended: assets referenced indirectly still get pulled). */
	UPROPERTY(EditAnywhere, config, Category = "Source", meta = (DisplayName = "Include Soft References"))
	bool bIncludeSoftReferences = true;

	FString GetSourceContentDir() const;
	void SetSourceContentDir(const FString& InDir);
	void SetIncludeSoftReferences(bool bInclude);

private:
	/** Persist a change made from the plugin window (not the details panel). */
	void SaveToConfig();
};
