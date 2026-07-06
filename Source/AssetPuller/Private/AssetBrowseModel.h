#pragma once

#include "AssetPullerTypes.h"
#include "CoreMinimal.h"

/**
 * Pure (no-Slate) model backing the window's Browse mode. Turns the flat filename index
 * into a mesh-only, pack/category tree derived entirely from each entry's RelPath and
 * filename prefix — the source project is never opened, and no file contents are read.
 *
 * Scope decision (see README / CLAUDE.md): Browse shows meshes only — static meshes
 * (SM_/geo_) and skeletal meshes (SK_). Materials, textures, VFX, maps, blueprints and
 * Epic template/mannequin content are excluded; materials/textures still ride along
 * automatically as dependencies when a mesh is imported.
 */

/** What kind of mesh an entry is, inferred from its filename prefix. */
enum class EBrowseMeshKind : uint8
{
	Static,
	Skeletal,
};

/** A single browsable mesh: the underlying index entry plus its derived pack/category/kind. */
struct FBrowseMesh
{
	/** The real index entry — this is what gets handed to the (untouched) import pipeline. */
	TSharedPtr<FDumpAssetEntry> Entry;

	/** Normalized pack id, e.g. "PolygonCyberCity". Synty/<pack> containers are flattened to <pack>. */
	FString PackKey;

	/** Category folder under the pack's type/wrapper folder(s); empty when the mesh is uncategorized. */
	FString Category;

	EBrowseMeshKind Kind = EBrowseMeshKind::Static;
};

/** A node in the Browse tree: either a pack (root) or a category within a pack. */
struct FBrowseNode
{
	FString DisplayName;
	FString PackKey;        // which pack this node belongs to
	FString Category;       // empty => this is the pack node (represents every mesh in the pack)
	bool bIsPack = false;
	int32 Count = 0;        // meshes at-or-below this node
	TArray<TSharedPtr<FBrowseNode>> Children;
};

namespace AssetBrowseModel
{
	/** True if this entry is a browsable mesh (SM_/SK_/geo_ prefix, not Epic/dev/collection content). */
	bool IsBrowsableMesh(const FDumpAssetEntry& Entry);

	/** Static for SM_/geo_, Skeletal for SK_. */
	EBrowseMeshKind GetMeshKind(const FString& AssetName);

	/** Splits a RelPath into its normalized pack and (first) category folder. */
	void ParsePackAndCategory(const FString& RelPath, FString& OutPack, FString& OutCategory);

	/**
	 * Builds the full mesh list and the pack -> category tree from raw index entries.
	 * OutMeshes/OutRootNodes are reset first. Roots and each pack's children are sorted by name.
	 */
	void Build(const TArray<TSharedPtr<FDumpAssetEntry>>& Entries,
		TArray<FBrowseMesh>& OutMeshes,
		TArray<TSharedPtr<FBrowseNode>>& OutRootNodes);
}
