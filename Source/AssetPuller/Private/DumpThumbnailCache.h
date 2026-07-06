#pragma once

#include "AssetPullerTypes.h"
#include "CoreMinimal.h"

struct FSlateDynamicImageBrush;
struct FSlateBrush;

/**
 * Lazily loads the thumbnails embedded in the source dump's .uasset files (read straight
 * from the package's thumbnail table on disk — the package is never loaded) and turns
 * them into Slate brushes for the search list. Rows are virtualized, so only visible
 * entries ever get loaded; results are cached, including "this package has no thumbnail".
 *
 * NOTE: ThumbnailTools::LoadThumbnailsFromPackage is NOT usable here — it fatally
 * asserts on files outside mounted content roots. FPackageReader::GetThumbnails is the
 * safe route for foreign files.
 */
class FDumpThumbnailCache
{
public:
	~FDumpThumbnailCache() { Reset(); }

	/**
	 * Brush for the entry's thumbnail, or null if the package has none. Game thread only.
	 * Callers must HOLD the shared pointer for as long as they display the brush —
	 * eviction may otherwise destroy it while a live row still points at it.
	 */
	TSharedPtr<FSlateDynamicImageBrush> GetThumbnail(const FDumpAssetEntry& Entry);

	/** Drops all brushes (releases their GPU resources via the brush destructors). */
	void Reset();

private:
	TSharedPtr<FSlateDynamicImageBrush> LoadThumbnail(const FDumpAssetEntry& Entry) const;

	/** Key: source file path. Null value = known to have no thumbnail. */
	TMap<FString, TSharedPtr<FSlateDynamicImageBrush>> Cache;

	static constexpr int32 MaxCachedThumbnails = 2048;
};
