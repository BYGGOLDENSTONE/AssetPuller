#pragma once

#include "CoreMinimal.h"
#include "AssetPullerTypes.h"

/**
 * Filename index of the source dump project's Content folder.
 * Built by scanning the directory tree directly — the source project is never opened,
 * so the data is always current (no stale registry problem).
 */
class FAssetDumpIndex
{
public:
	/** Scans SourceContentDir for .uasset/.umap files. Returns false if the folder is missing/empty. */
	bool Build(const FString& InSourceContentDir);

	bool IsBuilt() const { return bBuilt; }
	const FString& GetSourceContentDir() const { return SourceContentDir; }
	int32 Num() const { return Entries.Num(); }

	/**
	 * Ranked search. The query may contain several comma-separated terms (OR).
	 * Ranking per term: exact name match, then prefix, then substring. Case-insensitive.
	 * OutTotalMatches reports the uncapped match count.
	 */
	TArray<TSharedPtr<FDumpAssetEntry>> Search(const FString& Query, int32 MaxResults, int32& OutTotalMatches) const;

	/** All exact (case-insensitive) name matches, e.g. same name in different folders. */
	TArray<TSharedPtr<FDumpAssetEntry>> FindExact(const FString& AssetName) const;

	/** Entry whose package name is exactly PackageName (e.g. /Game/Foo/Bar), if indexed. */
	TSharedPtr<FDumpAssetEntry> FindByPackageName(const FString& PackageName) const;

private:
	bool bBuilt = false;
	FString SourceContentDir;
	TArray<TSharedPtr<FDumpAssetEntry>> Entries;
	TMap<FString, TArray<int32>> NameToIndices;        // lowercase asset name -> entry indices
	TMap<FString, int32> PackageNameToIndex;           // lowercase /Game/... -> entry index
};
