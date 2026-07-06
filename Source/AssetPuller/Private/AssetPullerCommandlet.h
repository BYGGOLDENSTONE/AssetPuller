#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"
#include "AssetPullerCommandlet.generated.h"

/**
 * Headless pull, e.g. for batch scripts:
 *   UnrealEditor-Cmd.exe <project.uproject> -run=AssetPuller -Names=SM_Bld_House_01,SM_Wep_Sword_01
 *     [-Source=<path to source project's Content folder>] [-NoSoft] [-Update] [-DryRun] [-VerifyLoad]
 *
 * -Source     overrides the source folder from Project Settings
 * -NoSoft     ignore soft references
 * -Update     overwrite existing assets whose content differs in the source (old files are
 *             backed up to Saved/AssetPullerBackups; maps are never updated)
 * -DryRun     resolve and print the plan without copying
 * -VerifyLoad after copying, fully load every copied package to prove references are intact
 */
UCLASS()
class UAssetPullerCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAssetPullerCommandlet()
	{
		IsClient = false;
		IsServer = false;
		IsEditor = true;
		LogToConsole = true;
	}

	virtual int32 Main(const FString& Params) override;
};
