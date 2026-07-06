#pragma once

#include "CoreMinimal.h"
#include "AssetPullerTypes.h"

/**
 * Filename index of the source dump project's Content folder.
 * Built by scanning the directory tree directly — the source project is never opened,
 * so the data is always current (no stale registry problem).
 *
 * Build() is pure filesystem work (no UObjects) and may run on a worker thread as long
 * as the instance isn't read concurrently. A built index can be cached to disk so the
 * window opens instantly on big libraries; a fresh background scan then replaces it.
 */
class FAssetDumpIndex
{
public:
	/** Scans SourceContentDir for .uasset/.umap files. Returns false if the folder is missing/empty. */
	bool Build(const FString& InSourceContentDir);

	/** Loads a previously saved index if it matches InSourceContentDir. */
	bool LoadFromCache(const FString& CacheFile, const FString& InSourceContentDir);

	/** Saves the built index. */
	void SaveToCache(const FString& CacheFile) const;

	bool IsBuilt() const { return bBuilt; }
	const FString& GetSourceContentDir() const { return SourceContentDir; }
	int32 Num() const { return Entries.Num(); }

	/** All indexed entries, for browsing the whole library by pack/category (not a search). */
	const TArray<TSharedPtr<FDumpAssetEntry>>& GetEntries() const { return Entries; }

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
	/** Rebuilds the lookup maps from Entries and marks the index built. */
	void FinalizeEntries();

	bool bBuilt = false;
	FString SourceContentDir;
	TArray<TSharedPtr<FDumpAssetEntry>> Entries;
	TMap<FString, TArray<int32>> NameToIndices;        // lowercase asset name -> entry indices
	TMap<FString, int32> PackageNameToIndex;           // lowercase /Game/... -> entry index

	static constexpr uint32 CacheMagic = 0x41504943;   // 'APIC'
	static constexpr uint32 CacheVersion = 2;
};
