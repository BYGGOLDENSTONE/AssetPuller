#include "AssetPullerCommandlet.h"

#include "AssetDependencyResolver.h"
#include "AssetDumpIndex.h"
#include "AssetPullerSettings.h"
#include "AssetPullExecutor.h"
#include "AssetPullerTypes.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

int32 UAssetPullerCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	FString NamesArg = ParamsMap.FindRef(TEXT("Names"));
	if (NamesArg.IsEmpty())
	{
		NamesArg = ParamsMap.FindRef(TEXT("Name"));
	}
	if (NamesArg.IsEmpty())
	{
		UE_LOG(LogAssetPuller, Error, TEXT("Usage: -run=AssetPuller -Names=AssetA,AssetB [-Source=<Content dir>] [-NoSoft] [-DryRun] [-VerifyLoad]"));
		return 1;
	}

	FAssetDependencyResolver::FParams ResolveParams;
	ResolveParams.SourceContentDir = ParamsMap.Contains(TEXT("Source"))
		? ParamsMap.FindRef(TEXT("Source"))
		: GetDefault<UAssetPullerSettings>()->GetSourceContentDir();
	FPaths::NormalizeDirectoryName(ResolveParams.SourceContentDir);
	ResolveParams.TargetContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	FPaths::NormalizeDirectoryName(ResolveParams.TargetContentDir);
	ResolveParams.bIncludeSoftReferences = !Switches.Contains(TEXT("NoSoft"));
	ResolveParams.bUpdateExisting = Switches.Contains(TEXT("Update"));

	if (FPaths::IsUnderDirectory(ResolveParams.SourceContentDir, ResolveParams.TargetContentDir) ||
		FPaths::IsUnderDirectory(ResolveParams.TargetContentDir, ResolveParams.SourceContentDir))
	{
		UE_LOG(LogAssetPuller, Error, TEXT("Source folder overlaps this project's own Content folder — point -Source at the asset dump project."));
		return 1;
	}

	FAssetDumpIndex Index;
	if (!Index.Build(ResolveParams.SourceContentDir))
	{
		UE_LOG(LogAssetPuller, Error, TEXT("No assets found in source folder: %s"), *ResolveParams.SourceContentDir);
		return 1;
	}
	UE_LOG(LogAssetPuller, Display, TEXT("Indexed %d assets from %s"), Index.Num(), *ResolveParams.SourceContentDir);

	// Collect requested entries; a name may match several assets in different folders — pull all of them.
	TArray<TSharedPtr<FDumpAssetEntry>> Requested;
	TArray<FString> Names;
	NamesArg.ParseIntoArray(Names, TEXT(","), true);
	bool bAnyNotFound = false;
	for (FString& Name : Names)
	{
		Name.TrimStartAndEndInline();
		TArray<TSharedPtr<FDumpAssetEntry>> Matches = Index.FindExact(Name);
		if (Matches.Num() == 0)
		{
			bAnyNotFound = true;
			UE_LOG(LogAssetPuller, Error, TEXT("Asset not found in source library: '%s'"), *Name);
			int32 TotalNear = 0;
			TArray<TSharedPtr<FDumpAssetEntry>> Near = Index.Search(Name, 5, TotalNear);
			for (const TSharedPtr<FDumpAssetEntry>& Candidate : Near)
			{
				UE_LOG(LogAssetPuller, Display, TEXT("  did you mean: %s  (%s)"), *Candidate->AssetName, *Candidate->RelPath);
			}
			continue;
		}
		if (Matches.Num() > 1)
		{
			UE_LOG(LogAssetPuller, Warning, TEXT("'%s' matches %d assets in different folders — pulling all of them."), *Name, Matches.Num());
		}
		Requested.Append(Matches);
	}
	if (Requested.Num() == 0)
	{
		return 1;
	}

	FPullPlan Plan = FAssetDependencyResolver::BuildPlan(Requested, ResolveParams, [](const FString&) {});

	UE_LOG(LogAssetPuller, Display, TEXT("Plan: %d to copy (%lld bytes), %d to update (%lld bytes), %d already exist, %d missing in source, %d map(s)"),
		Plan.NumToCopy, Plan.TotalCopyBytes, Plan.NumToUpdate, Plan.TotalUpdateBytes, Plan.NumExisting, Plan.NumMissing, Plan.NumMaps);
	for (const FPullItem& Item : Plan.Items)
	{
		const TCHAR* StatusStr =
			Item.Status == EPullItemStatus::CopyNew ? TEXT("COPY") :
			Item.Status == EPullItemStatus::UpdateExisting ? TEXT("UPDATE") :
			Item.Status == EPullItemStatus::SkipExists ? TEXT("SKIP") : TEXT("MISSING");
		UE_LOG(LogAssetPuller, Display, TEXT("  [%s]%s %s"), StatusStr, Item.bSoftOnly ? TEXT(" (soft)") : TEXT(""), *Item.PackageName);
	}

	if (Switches.Contains(TEXT("DryRun")))
	{
		UE_LOG(LogAssetPuller, Display, TEXT("DryRun — nothing copied."));
		return bAnyNotFound ? 1 : 0;
	}

	const FPullReport Report = FAssetPullExecutor::Execute(Plan, /*bShowProgressDialog*/false);
	if (!Report.BackupDir.IsEmpty())
	{
		UE_LOG(LogAssetPuller, Display, TEXT("Overwritten files backed up to: %s"), *Report.BackupDir);
	}

	int32 LoadFailures = 0;
	if (Switches.Contains(TEXT("VerifyLoad")))
	{
		TArray<FString> PackagesToVerify = Report.CopiedPackageNames;
		PackagesToVerify.Append(Report.UpdatedPackageNames);
		for (const FString& PackageName : PackagesToVerify)
		{
			UPackage* Loaded = LoadPackage(nullptr, *PackageName, LOAD_None);
			if (!Loaded)
			{
				LoadFailures++;
				UE_LOG(LogAssetPuller, Error, TEXT("VerifyLoad FAILED: %s"), *PackageName);
			}
		}
		UE_LOG(LogAssetPuller, Display, TEXT("VerifyLoad: %d/%d copied/updated packages loaded successfully"),
			PackagesToVerify.Num() - LoadFailures, PackagesToVerify.Num());
	}

	return (Report.NumFailed == 0 && LoadFailures == 0 && !bAnyNotFound) ? 0 : 1;
}
