#include "AssetDependencyResolver.h"

#include "AssetRegistry/PackageReader.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/ObjectResource.h"

namespace AssetPullerPrivate
{
	// A dependency is copyable only if it lives under the source project's own /Game/ root.
	// /Script/ (C++ classes) and /Engine/ etc. exist in every project and are never copied.
	static bool IsGamePackage(const FString& PackageName)
	{
		return PackageName.StartsWith(TEXT("/Game/"));
	}

	static FString RootOfPackage(const FString& PackageName)
	{
		const int32 SecondSlash = PackageName.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
		return SecondSlash != INDEX_NONE ? PackageName.Left(SecondSlash) : PackageName;
	}

	/**
	 * For "/Game/__ExternalActors__/<MapPath>/<a>/<bc>/<hash>" returns the owning map
	 * "/Game/<MapPath>" (both packaging schemes use 3 trailing segments: folder/folder/file).
	 */
	static bool GetOwningMapOfExternalPackage(const FString& PackageName, FString& OutMapPackageName)
	{
		const TCHAR* Roots[] = { TEXT("/Game/__ExternalActors__/"), TEXT("/Game/__ExternalObjects__/") };
		for (const TCHAR* Root : Roots)
		{
			if (!PackageName.StartsWith(Root))
			{
				continue;
			}
			TArray<FString> Segments;
			PackageName.Mid(FCString::Strlen(Root)).ParseIntoArray(Segments, TEXT("/"));
			if (Segments.Num() < 4)  // need at least <MapName>/<a>/<bc>/<hash>
			{
				return false;
			}
			Segments.SetNum(Segments.Num() - 3);
			OutMapPackageName = TEXT("/Game/") + FString::Join(Segments, TEXT("/"));
			return true;
		}
		return false;
	}
}

FPullPlan FAssetDependencyResolver::BuildPlan(const TArray<TSharedPtr<FDumpAssetEntry>>& Requested,
                                              const FParams& Params,
                                              TFunctionRef<void(const FString&)> ProgressCallback)
{
	FPullPlan Plan;

	struct FVisit
	{
		FString PackageName;
		bool bIsRequested = false;
	};
	TArray<FVisit> Queue;
	TMap<FString, int32> VisitedToItemIndex;         // lowercase package name -> Plan.Items index
	TMap<FString, TArray<FString>> HardEdges;        // lowercase package name -> hard dep package names
	TSet<FString> ExternalRoots;
	TArray<FString> RequestedKeys;

	for (const TSharedPtr<FDumpAssetEntry>& Entry : Requested)
	{
		Queue.Add({ Entry->GetPackageName(), /*bIsRequested*/true });
		RequestedKeys.AddUnique(Entry->GetPackageName().ToLower());
	}

	while (Queue.Num() > 0)
	{
		const FVisit Visit = Queue.Pop(EAllowShrinking::No);
		const FString Key = Visit.PackageName.ToLower();

		if (int32* ExistingIndex = VisitedToItemIndex.Find(Key))
		{
			if (Visit.bIsRequested) { Plan.Items[*ExistingIndex].bIsRequested = true; }
			continue;
		}

		if (!AssetPullerPrivate::IsGamePackage(Visit.PackageName))
		{
			ExternalRoots.Add(AssetPullerPrivate::RootOfPackage(Visit.PackageName));
			continue;
		}

		ProgressCallback(Visit.PackageName);

		FPullItem Item;
		Item.PackageName = Visit.PackageName;
		Item.bIsRequested = Visit.bIsRequested;

		bool bIsMap = false;
		Item.SourceFile = PackageNameToExistingFile(Params.SourceContentDir, Visit.PackageName, bIsMap);
		Item.bIsMap = bIsMap;

		const FString RelNoExt = Visit.PackageName.Mid(FCString::Strlen(TEXT("/Game/")));

		// The target may hold this package under EITHER extension (.uasset/.umap) —
		// probing only one would let a copy land next to an existing sibling and
		// leave two files claiming the same package name.
		bool bTargetIsMap = false;
		const FString ExistingTargetFile = PackageNameToExistingFile(Params.TargetContentDir, Visit.PackageName, bTargetIsMap);

		if (Item.SourceFile.IsEmpty())
		{
			// Not in the dump. If the target project already has it, the reference is
			// satisfied anyway — only report MISSING when it exists on neither side.
			if (!ExistingTargetFile.IsEmpty())
			{
				Item.Status = EPullItemStatus::SkipExists;
				Item.TargetFile = ExistingTargetFile;
				Item.bIsMap = bTargetIsMap;
				Item.RelPathWithExt = RelNoExt + FPaths::GetExtension(ExistingTargetFile, /*bIncludeDot*/true);
			}
			else
			{
				Item.Status = EPullItemStatus::MissingInSource;
				Item.RelPathWithExt = RelNoExt + TEXT(".uasset");
			}
			VisitedToItemIndex.Add(Key, Plan.Items.Add(Item));
			continue;
		}

		const FString Ext = FPaths::GetExtension(Item.SourceFile, /*bIncludeDot*/true);
		Item.RelPathWithExt = RelNoExt + Ext;
		Item.FileSize = IFileManager::Get().FileSize(*Item.SourceFile);
		if (!ExistingTargetFile.IsEmpty())
		{
			Item.Status = EPullItemStatus::SkipExists;
			Item.TargetFile = ExistingTargetFile;
		}
		else
		{
			Item.Status = EPullItemStatus::CopyNew;
			Item.TargetFile = Params.TargetContentDir / Item.RelPathWithExt;
		}

		const int32 ItemIndex = Plan.Items.Add(Item);
		VisitedToItemIndex.Add(Key, ItemIndex);

		// Even when the package already exists in the target we still walk its dependencies:
		// a previous manual/partial copy may have holes this pull can fill.
		TArray<FName> HardDeps, SoftDeps;
		FString ReadError;
		if (!ReadPackageRefs(Item.SourceFile, Visit.PackageName, HardDeps, SoftDeps, ReadError))
		{
			UE_LOG(LogAssetPuller, Warning, TEXT("Could not read dependencies of %s: %s"), *Visit.PackageName, *ReadError);
			continue;
		}

		TArray<FString>& OutHardEdges = HardEdges.Add(Key);
		for (const FName& Dep : HardDeps)
		{
			FString DepName = Dep.ToString();
			OutHardEdges.Add(DepName);
			Queue.Add({ MoveTemp(DepName), false });
		}
		if (Params.bIncludeSoftReferences)
		{
			for (const FName& Dep : SoftDeps)
			{
				Queue.Add({ Dep.ToString(), false });
			}
		}

		// Maps saved with One File Per Actor keep their actors in external packages that are
		// NOT in the map's import table — pick them up by folder convention. Only for maps we
		// actually copy: the engine discovers OFPA actors by scanning these folders, so copying
		// them for a map that already exists in the target would inject actors into that map.
		if (Item.bIsMap && Plan.Items[ItemIndex].Status == EPullItemStatus::CopyNew)
		{
			TArray<FString> ExternalPackages;
			CollectMapExternalPackages(Params.SourceContentDir, Visit.PackageName, ExternalPackages);
			for (FString& External : ExternalPackages)
			{
				// Part of the map itself: traverse and label as hard dependencies.
				OutHardEdges.Add(External);
				Queue.Add({ MoveTemp(External), false });
			}
		}
	}

	// "Soft only" labeling pass: a package is soft-only when it is NOT reachable from the
	// requested assets through hard edges alone (i.e. it would not be pulled with soft refs off).
	{
		TSet<FString> HardReachable;
		TArray<FString> HardQueue = RequestedKeys;
		while (HardQueue.Num() > 0)
		{
			const FString HardKey = HardQueue.Pop(EAllowShrinking::No);
			bool bAlready = false;
			HardReachable.Add(HardKey, &bAlready);
			if (bAlready)
			{
				continue;
			}
			if (const TArray<FString>* Deps = HardEdges.Find(HardKey))
			{
				for (const FString& Dep : *Deps)
				{
					HardQueue.Add(Dep.ToLower());
				}
			}
		}
		for (TPair<FString, int32>& Pair : VisitedToItemIndex)
		{
			Plan.Items[Pair.Value].bSoftOnly = !HardReachable.Contains(Pair.Key);
		}
	}

	// External actor/object packages belong to their map. The engine materializes EVERY package
	// found in those folders as an actor of the map, so copying one next to a map that already
	// exists in the target would silently inject (or resurrect deleted) actors — no matter how
	// the package entered the closure (folder collection or a soft reference in the source map).
	// Rule: only copy them when their owning map is itself copied in this plan.
	{
		TSet<FString> MapsBeingCopied;
		for (const FPullItem& Item : Plan.Items)
		{
			if (Item.bIsMap && Item.Status == EPullItemStatus::CopyNew)
			{
				MapsBeingCopied.Add(Item.PackageName.ToLower());
			}
		}
		Plan.Items.RemoveAll([&MapsBeingCopied](const FPullItem& Item)
		{
			if (Item.Status != EPullItemStatus::CopyNew)
			{
				return false;
			}
			FString OwningMap;
			if (!AssetPullerPrivate::GetOwningMapOfExternalPackage(Item.PackageName, OwningMap))
			{
				return false;
			}
			if (MapsBeingCopied.Contains(OwningMap.ToLower()))
			{
				return false;
			}
			UE_LOG(LogAssetPuller, Log, TEXT("Not copying %s: it belongs to map %s, which is not being copied in this operation."),
				*Item.PackageName, *OwningMap);
			return true;
		});
	}

	Plan.ExternalRefs = ExternalRoots.Array();
	Plan.ExternalRefs.Sort();

	// Requested assets first, then copies, then skips, then missing — reads naturally in the confirm list.
	Plan.Items.Sort([](const FPullItem& A, const FPullItem& B)
	{
		if (A.bIsRequested != B.bIsRequested) { return A.bIsRequested; }
		if (A.Status != B.Status) { return static_cast<uint8>(A.Status) < static_cast<uint8>(B.Status); }
		return A.PackageName < B.PackageName;
	});

	Plan.RecountTotals();
	return Plan;
}

bool FAssetDependencyResolver::ReadPackageRefs(const FString& PackageFile, const FString& LongPackageName,
                                               TArray<FName>& OutHardDeps, TArray<FName>& OutSoftDeps, FString& OutError)
{
	FPackageReader Reader;
	FPackageReader::EOpenPackageResult OpenResult;
	if (!Reader.OpenPackageFile(FStringView(LongPackageName), FStringView(PackageFile), &OpenResult))
	{
		switch (OpenResult)
		{
		case FPackageReader::EOpenPackageResult::VersionTooNew:
			OutError = TEXT("saved with a NEWER engine version than this editor — update this project's engine");
			break;
		case FPackageReader::EOpenPackageResult::VersionTooOld:
			OutError = TEXT("saved with an engine version too old to read");
			break;
		default:
			OutError = FString::Printf(TEXT("could not be opened as an Unreal package (%s)"), LexToString(OpenResult));
			break;
		}
		return false;
	}

	// Hard dependencies = top-level package entries of the import table
	// (an import with no outer is a package, e.g. /Game/Foo, /Script/Engine).
	TArray<FObjectImport> Imports;
	if (!Reader.GetImports(Imports))
	{
		OutError = TEXT("import table could not be read");
		return false;
	}
	for (const FObjectImport& Import : Imports)
	{
		if (Import.OuterIndex.IsNull() && !Import.ObjectName.IsNone())
		{
			OutHardDeps.Add(Import.ObjectName);
		}
	}

	TArray<FName> SoftRefs;
	if (Reader.GetSoftPackageReferenceList(SoftRefs))
	{
		OutSoftDeps = MoveTemp(SoftRefs);
	}
	return true;
}

FString FAssetDependencyResolver::PackageNameToExistingFile(const FString& ContentDir, const FString& PackageName, bool& bOutIsMap)
{
	bOutIsMap = false;
	const FString Rel = PackageName.Mid(FCString::Strlen(TEXT("/Game/")));

	FString Candidate = ContentDir / Rel + TEXT(".uasset");
	if (IFileManager::Get().FileExists(*Candidate))
	{
		return Candidate;
	}
	Candidate = ContentDir / Rel + TEXT(".umap");
	if (IFileManager::Get().FileExists(*Candidate))
	{
		bOutIsMap = true;
		return Candidate;
	}
	return FString();
}

void FAssetDependencyResolver::CollectMapExternalPackages(const FString& SourceContentDir, const FString& MapPackageName,
                                                          TArray<FString>& OutPackagesToVisit)
{
	// Layout convention (see ULevel::GetExternalActorsPath):
	//   map  /Game/Maps/Demo  ->  Content/__ExternalActors__/Maps/Demo/**  and  Content/__ExternalObjects__/Maps/Demo/**
	const FString MapRel = MapPackageName.Mid(FCString::Strlen(TEXT("/Game/")));

	const TCHAR* ExternalFolders[] = { TEXT("__ExternalActors__"), TEXT("__ExternalObjects__") };
	for (const TCHAR* Folder : ExternalFolders)
	{
		const FString ExternalDir = SourceContentDir / Folder / MapRel;
		if (!IFileManager::Get().DirectoryExists(*ExternalDir))
		{
			continue;
		}

		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *ExternalDir, TEXT("*.uasset"), true, false);
		for (FString& File : Files)
		{
			FPaths::NormalizeFilename(File);
			FString Rel = File.Mid(SourceContentDir.Len() + 1);
			Rel = Rel.LeftChop(FCString::Strlen(TEXT(".uasset")));
			OutPackagesToVisit.Add(TEXT("/Game/") + Rel);
		}
	}
}
