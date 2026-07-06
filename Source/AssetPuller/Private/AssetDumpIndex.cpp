#include "AssetDumpIndex.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"

namespace
{
	// Bounded string (de)serialization: the cache file is external input — a corrupt or
	// hostile length prefix must never drive an allocation.
	void WriteCacheString(FArchive& Ar, const FString& Str)
	{
		FTCHARToUTF8 Converted(*Str);
		int32 Len = Converted.Length();
		Ar << Len;
		Ar.Serialize(const_cast<ANSICHAR*>(Converted.Get()), Len);
	}

	bool ReadCacheString(FArchive& Ar, FString& OutStr, int32 MaxLen = 4096)
	{
		int32 Len = 0;
		Ar << Len;
		if (Ar.IsError() || Len < 0 || Len > MaxLen || Ar.Tell() + Len > Ar.TotalSize())
		{
			return false;
		}
		TArray<ANSICHAR> Buffer;
		Buffer.SetNumUninitialized(Len + 1);
		Ar.Serialize(Buffer.GetData(), Len);
		Buffer[Len] = 0;
		OutStr = UTF8_TO_TCHAR(Buffer.GetData());
		return !Ar.IsError();
	}
}

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

		FString Rel = File.Mid(Prefix.Len());

		// Hash-named per-actor map packages: reachable through map dependency traversal,
		// but pure noise in a name search — keep them out of the index.
		if (Rel.StartsWith(TEXT("__ExternalActors__/")) || Rel.StartsWith(TEXT("__ExternalObjects__/")))
		{
			continue;
		}

		TSharedPtr<FDumpAssetEntry> Entry = MakeShared<FDumpAssetEntry>();
		Entry->SourceFile = File;
		Entry->bIsMap = File.EndsWith(TEXT(".umap"));

		const int32 DotIndex = Rel.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		Entry->RelPath = (DotIndex != INDEX_NONE) ? Rel.Left(DotIndex) : Rel;
		Entry->AssetName = FPaths::GetBaseFilename(File);
		Entry->AssetNameLower = Entry->AssetName.ToLower();

		Entries.Add(MoveTemp(Entry));
	}

	FinalizeEntries();
	return bBuilt;
}

void FAssetDumpIndex::FinalizeEntries()
{
	NameToIndices.Reset();
	PackageNameToIndex.Reset();
	NameToIndices.Reserve(Entries.Num());
	PackageNameToIndex.Reserve(Entries.Num());

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const TSharedPtr<FDumpAssetEntry>& Entry = Entries[Index];
		NameToIndices.FindOrAdd(Entry->AssetNameLower).Add(Index);
		PackageNameToIndex.Add(Entry->GetPackageName().ToLower(), Index);
	}

	bBuilt = Entries.Num() > 0;
}

bool FAssetDumpIndex::LoadFromCache(const FString& CacheFile, const FString& InSourceContentDir)
{
	bBuilt = false;
	Entries.Reset();

	FString WantedDir = InSourceContentDir;
	FPaths::NormalizeDirectoryName(WantedDir);
	if (WantedDir.IsEmpty())
	{
		return false;
	}

	TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*CacheFile));
	if (!Ar)
	{
		return false;
	}

	uint32 Magic = 0, Version = 0;
	int32 Count = 0;
	*Ar << Magic << Version;
	FString CachedDir;
	if (Ar->IsError() || Magic != CacheMagic || Version != CacheVersion || !ReadCacheString(*Ar, CachedDir))
	{
		return false;
	}
	*Ar << Count;
	// Each entry occupies at least ~10 bytes on disk — a count beyond that bound is corrupt.
	if (Ar->IsError() || Count < 0 || static_cast<int64>(Count) > Ar->TotalSize() / 10
		|| !CachedDir.Equals(WantedDir, ESearchCase::IgnoreCase))
	{
		return false;
	}

	SourceContentDir = WantedDir;
	Entries.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		TSharedPtr<FDumpAssetEntry> Entry = MakeShared<FDumpAssetEntry>();
		uint8 bIsMap = 0;
		if (!ReadCacheString(*Ar, Entry->AssetName) || !ReadCacheString(*Ar, Entry->RelPath))
		{
			Entries.Reset();
			return false;
		}
		*Ar << bIsMap;
		if (Ar->IsError())
		{
			Entries.Reset();
			return false;
		}
		Entry->bIsMap = bIsMap != 0;
		Entry->AssetNameLower = Entry->AssetName.ToLower();
		Entry->SourceFile = SourceContentDir / Entry->RelPath + (Entry->bIsMap ? TEXT(".umap") : TEXT(".uasset"));
		Entries.Add(MoveTemp(Entry));
	}

	FinalizeEntries();
	return bBuilt;
}

void FAssetDumpIndex::SaveToCache(const FString& CacheFile) const
{
	if (!bBuilt)
	{
		return;
	}

	TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(*CacheFile));
	if (!Ar)
	{
		UE_LOG(LogAssetPuller, Warning, TEXT("Could not write index cache to %s"), *CacheFile);
		return;
	}

	uint32 Magic = CacheMagic, Version = CacheVersion;
	int32 Count = Entries.Num();
	*Ar << Magic << Version;
	WriteCacheString(*Ar, SourceContentDir);
	*Ar << Count;
	for (const TSharedPtr<FDumpAssetEntry>& Entry : Entries)
	{
		uint8 bIsMap = Entry->bIsMap ? 1 : 0;
		WriteCacheString(*Ar, Entry->AssetName);
		WriteCacheString(*Ar, Entry->RelPath);
		*Ar << bIsMap;
	}
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
		if (BestRank < 3)
		{
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
