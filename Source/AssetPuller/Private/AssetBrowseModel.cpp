#include "AssetBrowseModel.h"

namespace
{
	bool HasPrefixCI(const FString& Name, const TCHAR* Prefix)
	{
		return Name.StartsWith(Prefix, ESearchCase::IgnoreCase);
	}

	/**
	 * Pure containers whose name carries no browsing meaning — we look one level deeper for
	 * the category. Everything else (Buildings, Weapons, Characters, Dragon, ...) is treated
	 * as a category, so e.g. Meshes/Characters and a top-level Characters/ folder both land
	 * under a "Characters" category.
	 */
	bool IsContainerFolder(const FString& Segment)
	{
		return Segment.Equals(TEXT("Meshes"), ESearchCase::IgnoreCase)
			|| Segment.Equals(TEXT("Models"), ESearchCase::IgnoreCase)
			|| Segment.Equals(TEXT("PNB_Core"), ESearchCase::IgnoreCase);
	}
}

namespace AssetBrowseModel
{
	bool IsBrowsableMesh(const FDumpAssetEntry& Entry)
	{
		if (Entry.bIsMap)
		{
			return false;
		}

		const FString& Name = Entry.AssetName;
		// StartsWith("SK_") already excludes SKEL_ (skeleton assets), which are dependencies, not meshes.
		const bool bMesh = HasPrefixCI(Name, TEXT("SM_")) || HasPrefixCI(Name, TEXT("SK_")) || HasPrefixCI(Name, TEXT("geo_"));
		if (!bMesh)
		{
			return false;
		}

		// Epic template/mannequin and editor-only folders are not Synty art.
		if (Entry.RelPath.Contains(TEXT("/EpicContent/"), ESearchCase::IgnoreCase)
			|| Entry.RelPath.StartsWith(TEXT("EpicContent/"), ESearchCase::IgnoreCase)
			|| Entry.RelPath.StartsWith(TEXT("Developers/"), ESearchCase::IgnoreCase)
			|| Entry.RelPath.StartsWith(TEXT("Collections/"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		return true;
	}

	EBrowseMeshKind GetMeshKind(const FString& AssetName)
	{
		return HasPrefixCI(AssetName, TEXT("SK_")) ? EBrowseMeshKind::Skeletal : EBrowseMeshKind::Static;
	}

	void ParsePackAndCategory(const FString& RelPath, FString& OutPack, FString& OutCategory)
	{
		OutPack.Reset();
		OutCategory.Reset();

		TArray<FString> Segs;
		RelPath.ParseIntoArray(Segs, TEXT("/"), /*CullEmpty*/true);
		if (Segs.Num() < 2)
		{
			// No pack folder (asset sits at the Content root) — nothing sensible to group under.
			return;
		}

		// The last segment is the asset name; folder segments are [0 .. LastFolder).
		const int32 LastFolder = Segs.Num() - 1;

		int32 Idx = 0;
		if (Segs[0].Equals(TEXT("Synty"), ESearchCase::IgnoreCase) && Segs.Num() >= 3)
		{
			// Synty/ is a container of more packs — flatten it up so <pack> becomes the pack.
			OutPack = Segs[1];
			Idx = 2;
		}
		else
		{
			OutPack = Segs[0];
			Idx = 1;
		}

		// Skip pure container/wrapper folders (Meshes, Models, PNB_Core) to reach the category.
		while (Idx < LastFolder && IsContainerFolder(Segs[Idx]))
		{
			++Idx;
		}
		if (Idx < LastFolder)
		{
			OutCategory = Segs[Idx];
		}
	}

	void Build(const TArray<TSharedPtr<FDumpAssetEntry>>& Entries,
		TArray<FBrowseMesh>& OutMeshes,
		TArray<TSharedPtr<FBrowseNode>>& OutRootNodes)
	{
		OutMeshes.Reset();
		OutRootNodes.Reset();

		// 1. Classify every browsable mesh.
		for (const TSharedPtr<FDumpAssetEntry>& Entry : Entries)
		{
			if (!Entry.IsValid() || !IsBrowsableMesh(*Entry))
			{
				continue;
			}

			FBrowseMesh Mesh;
			Mesh.Entry = Entry;
			Mesh.Kind = GetMeshKind(Entry->AssetName);
			ParsePackAndCategory(Entry->RelPath, Mesh.PackKey, Mesh.Category);
			if (Mesh.PackKey.IsEmpty())
			{
				continue;
			}
			OutMeshes.Add(MoveTemp(Mesh));
		}

		// 2. Accumulate pack nodes and per-pack category nodes with counts.
		TMap<FString, TSharedPtr<FBrowseNode>> PackNodes;
		TMap<FString, TMap<FString, TSharedPtr<FBrowseNode>>> CategoryNodes; // pack -> (category -> node)

		for (const FBrowseMesh& Mesh : OutMeshes)
		{
			TSharedPtr<FBrowseNode>& Pack = PackNodes.FindOrAdd(Mesh.PackKey);
			if (!Pack.IsValid())
			{
				Pack = MakeShared<FBrowseNode>();
				Pack->DisplayName = Mesh.PackKey;
				Pack->PackKey = Mesh.PackKey;
				Pack->bIsPack = true;
			}
			Pack->Count++;

			if (!Mesh.Category.IsEmpty())
			{
				TMap<FString, TSharedPtr<FBrowseNode>>& Cats = CategoryNodes.FindOrAdd(Mesh.PackKey);
				TSharedPtr<FBrowseNode>& Cat = Cats.FindOrAdd(Mesh.Category);
				if (!Cat.IsValid())
				{
					Cat = MakeShared<FBrowseNode>();
					Cat->DisplayName = Mesh.Category;
					Cat->PackKey = Mesh.PackKey;
					Cat->Category = Mesh.Category;
					Cat->bIsPack = false;
				}
				Cat->Count++;
			}
		}

		// 3. Attach sorted category children to packs, then sort the packs.
		for (const TPair<FString, TSharedPtr<FBrowseNode>>& PackPair : PackNodes)
		{
			const TSharedPtr<FBrowseNode>& Pack = PackPair.Value;
			if (TMap<FString, TSharedPtr<FBrowseNode>>* Cats = CategoryNodes.Find(Pack->PackKey))
			{
				Cats->GenerateValueArray(Pack->Children);
				Pack->Children.Sort([](const TSharedPtr<FBrowseNode>& A, const TSharedPtr<FBrowseNode>& B)
				{
					return A->DisplayName < B->DisplayName;
				});
			}
			OutRootNodes.Add(Pack);
		}

		OutRootNodes.Sort([](const TSharedPtr<FBrowseNode>& A, const TSharedPtr<FBrowseNode>& B)
		{
			return A->DisplayName < B->DisplayName;
		});
	}
}
