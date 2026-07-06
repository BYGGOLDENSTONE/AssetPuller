#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAssetPuller, Log, All);

/** One asset found in the source dump project's Content folder. */
struct FDumpAssetEntry
{
	/** Asset name without extension, e.g. "SM_Bld_House_01". */
	FString AssetName;

	/** Lowercase copy, computed once at index time — search runs per keystroke over 100k+ entries. */
	FString AssetNameLower;

	/** Path relative to the source Content folder, forward slashes, no extension, e.g. "PolygonFantasy/Meshes/SM_Bld_House_01". */
	FString RelPath;

	/** Absolute path of the .uasset/.umap file on disk. */
	FString SourceFile;

	bool bIsMap = false;

	/** Package name this file has inside its own project, e.g. "/Game/PolygonFantasy/Meshes/SM_Bld_House_01". */
	FString GetPackageName() const { return TEXT("/Game/") + RelPath; }
};

enum class EPullItemStatus : uint8
{
	/** Not present in the target project: will be copied. */
	CopyNew,
	/** Already exists at the same path in the target project: skipped, never overwritten. */
	SkipExists,
	/** Referenced by something in the chain but its file is missing in the source dump. */
	MissingInSource,
};

/** One package in a resolved pull plan (the requested asset or one of its dependencies). */
struct FPullItem
{
	FString PackageName;          // e.g. /Game/PolygonFantasy/Materials/M_Wood
	FString RelPathWithExt;       // e.g. PolygonFantasy/Materials/M_Wood.uasset
	FString SourceFile;           // absolute source file (empty when MissingInSource)
	FString TargetFile;           // absolute target file
	EPullItemStatus Status = EPullItemStatus::CopyNew;
	bool bSoftOnly = false;       // reached only through soft references
	bool bIsMap = false;
	bool bIsRequested = false;    // explicitly requested by the user (vs. pulled in as a dependency)
	int64 FileSize = 0;
};

/** Full resolved plan shown to the user before anything is copied. */
struct FPullPlan
{
	TArray<FPullItem> Items;

	/** Referenced package roots outside /Game/ (e.g. /Engine/...) — informational, never copied. */
	TArray<FString> ExternalRefs;

	int32 NumToCopy = 0;
	int32 NumExisting = 0;
	int32 NumMissing = 0;
	int32 NumMaps = 0;
	int64 TotalCopyBytes = 0;

	void RecountTotals()
	{
		NumToCopy = NumExisting = NumMissing = NumMaps = 0;
		TotalCopyBytes = 0;
		for (const FPullItem& Item : Items)
		{
			switch (Item.Status)
			{
			case EPullItemStatus::CopyNew:
				NumToCopy++;
				TotalCopyBytes += Item.FileSize;
				if (Item.bIsMap) { NumMaps++; }
				break;
			case EPullItemStatus::SkipExists:      NumExisting++; break;
			case EPullItemStatus::MissingInSource: NumMissing++;  break;
			}
		}
	}
};

/** Result of executing a pull plan. */
struct FPullReport
{
	int32 NumCopied = 0;
	int32 NumSkipped = 0;
	int32 NumFailed = 0;
	bool bCancelled = false;
	TArray<FString> FailedFiles;
	TArray<FString> CopiedTargetFiles;   // absolute target paths, for the registry rescan
	TArray<FString> CopiedPackageNames;
};
