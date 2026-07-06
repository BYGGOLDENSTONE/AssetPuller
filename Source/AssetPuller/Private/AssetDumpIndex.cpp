#include "AssetDumpIndex.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

bool FAssetDumpIndex::Build(const FString& InSourceContentDir)
{
	bBuilt = false;
	Entries.Reset();
	NameToIndices.Reset();
	PackageNameToIndex.Reset();

	SourceContentDir = InSourceContentDir;
	FPaths::NormalizeDirectoryName(SourceContentDir);

	if (SourceContentDir.IsEmpty() || !IFileManager::Get().DirectoryExists(*SourceContentDir))
	{
		return false;
	}

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *SourceContentDir, TEXT("*.uasset"), /*Files*/true, /*Dirs*/false);
	IFileManager::Get().FindFilesRecursive(Files, *SourceContentDir, TEXT("*.umap"), /*Files*/true, /*Dirs*/false, /*bClearFileNames*/false);

	Entries.Reserve(Files.Num());
	const FString Prefix = SourceContentDir + TEXT("/");

	for (FString& File : Files)
	{
		FPaths::NormalizeFilename(File);
		if (!File.StartsWith(Prefix))
		{
			continue;
		}

		TSharedPtr<FDumpAssetEntry> Entry = MakeShared<FDumpAssetEntry>();
		Entry->SourceFile = File;
		Entry->bIsMap = File.EndsWith(TEXT(".umap"));

		FString Rel = File.Mid(Prefix.Len());

		// Hash-named per-actor map packages: reachable through map dependency traversal,
		// but pure noise in a name search — keep them out of the index.
		if (Rel.StartsWith(TEXT("__ExternalActors__/")) || Rel.StartsWith(TEXT("__ExternalObjects__/")))
		{
			continue;
		}

		const int32 DotIndex = Rel.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		Entry->RelPath = (DotIndex != INDEX_NONE) ? Rel.Left(DotIndex) : Rel;
		Entry->AssetName = FPaths::GetBaseFilename(File);
		Entry->AssetNameLower = Entry->AssetName.ToLower();

		const int32 Index = Entries.Add(Entry);
		NameToIndices.FindOrAdd(Entry->AssetNameLower).Add(Index);
		PackageNameToIndex.Add(Entry->GetPackageName().ToLower(), Index);
	}

	// Stable alphabetical order so search results look deterministic.
	// (Indices in the maps must be built after sorting, so sort a copy instead — cheaper: sort at search time.)
	bBuilt = Entries.Num() > 0;
	return bBuilt;
}

TArray<TSharedPtr<FDumpAssetEntry>> FAssetDumpIndex::Search(const FString& Query, int32 MaxResults, int32& OutTotalMatches) const
{
	OutTotalMatches = 0;
	TArray<TSharedPtr<FDumpAssetEntry>> Results;
	if (!bBuilt)
	{
		return Results;
	}

	// Split "SM_A, SM_B" into OR terms.
	TArray<FString> Terms;
	Query.ParseIntoArray(Terms, TEXT(","), /*CullEmpty*/true);
	for (FString& Term : Terms)
	{
		Term.TrimStartAndEndInline();
		Term.ToLowerInline();
	}
	Terms.RemoveAll([](const FString& T) { return T.IsEmpty(); });
	if (Terms.Num() == 0)
	{
		return Results;
	}

	struct FScored
	{
		TSharedPtr<FDumpAssetEntry> Entry;
		int32 Rank = 3; // 0 exact, 1 prefix, 2 substring
	};
	TArray<FScored> Scored;
	TSet<const FDumpAssetEntry*> Seen;

	for (const TSharedPtr<FDumpAssetEntry>& Entry : Entries)
	{
		const FString& NameLower = Entry->AssetNameLower;
		int32 BestRank = 3;
		for (const FString& Term : Terms)
		{
			int32 Rank = 3;
			if (NameLower == Term) { Rank = 0; }
			else if (NameLower.StartsWith(Term)) { Rank = 1; }
			else if (NameLower.Contains(Term)) { Rank = 2; }
			BestRank = FMath::Min(BestRank, Rank);
		}
		if (BestRank < 3 && !Seen.Contains(Entry.Get()))
		{
			Seen.Add(Entry.Get());
			Scored.Add({ Entry, BestRank });
		}
	}

	OutTotalMatches = Scored.Num();
	Scored.Sort([](const FScored& A, const FScored& B)
	{
		if (A.Rank != B.Rank) { return A.Rank < B.Rank; }
		return A.Entry->AssetName < B.Entry->AssetName;
	});

	const int32 Count = FMath::Min(MaxResults, Scored.Num());
	Results.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		Results.Add(Scored[i].Entry);
	}
	return Results;
}

TArray<TSharedPtr<FDumpAssetEntry>> FAssetDumpIndex::FindExact(const FString& AssetName) const
{
	TArray<TSharedPtr<FDumpAssetEntry>> Results;
	if (const TArray<int32>* Indices = NameToIndices.Find(AssetName.ToLower()))
	{
		for (int32 Index : *Indices)
		{
			Results.Add(Entries[Index]);
		}
	}
	return Results;
}

TSharedPtr<FDumpAssetEntry> FAssetDumpIndex::FindByPackageName(const FString& PackageName) const
{
	if (const int32* Index = PackageNameToIndex.Find(PackageName.ToLower()))
	{
		return Entries[*Index];
	}
	return nullptr;
}
