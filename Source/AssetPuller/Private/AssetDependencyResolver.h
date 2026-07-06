#pragma once

#include "CoreMinimal.h"
#include "AssetPullerTypes.h"

/**
 * Resolves the full transitive dependency closure of the requested assets by reading
 * package file headers (import tables + soft reference lists) straight from the source
 * dump's files on disk. Nothing is ever loaded into the current project during resolve.
 */
class FAssetDependencyResolver
{
public:
	struct FParams
	{
		FString SourceContentDir;   // absolute, normalized, no trailing slash
		FString TargetContentDir;   // absolute, normalized (the current project's Content dir)
		bool bIncludeSoftReferences = true;
	};

	/**
	 * Builds the pull plan for the requested entries.
	 * ProgressCallback (optional) is called once per package header read, with the package name.
	 */
	static FPullPlan BuildPlan(const TArray<TSharedPtr<FDumpAssetEntry>>& Requested,
	                           const FParams& Params,
	                           TFunctionRef<void(const FString&)> ProgressCallback);

private:
	/** Reads hard imports + soft package refs from one package file. Returns false with OutError on failure. */
	static bool ReadPackageRefs(const FString& PackageFile, const FString& LongPackageName,
	                            TArray<FName>& OutHardDeps, TArray<FName>& OutSoftDeps, FString& OutError);

	/** Maps "/Game/Rel/Path" to an existing file under ContentDir (tries .uasset then .umap). Empty if missing. */
	static FString PackageNameToExistingFile(const FString& ContentDir, const FString& PackageName, bool& bOutIsMap);

	/** Collects all external actor/object packages that belong to a map package (One File Per Actor support). */
	static void CollectMapExternalPackages(const FString& SourceContentDir, const FString& MapPackageName,
	                                       TArray<FString>& OutPackagesToVisit);
};
