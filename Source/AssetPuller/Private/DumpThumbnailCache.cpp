#include "DumpThumbnailCache.h"

#include "AssetRegistry/PackageReader.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Misc/ObjectThumbnail.h"

TSharedPtr<FSlateDynamicImageBrush> FDumpThumbnailCache::GetThumbnail(const FDumpAssetEntry& Entry)
{
	if (const TSharedPtr<FSlateDynamicImageBrush>* Cached = Cache.Find(Entry.SourceFile))
	{
		return *Cached;
	}

	if (Cache.Num() >= MaxCachedThumbnails)
	{
		// Rows hold their own shared reference, so brushes still on screen survive this.
		Reset();
	}

	TSharedPtr<FSlateDynamicImageBrush> Brush = LoadThumbnail(Entry);
	Cache.Add(Entry.SourceFile, Brush);
	return Brush;
}

void FDumpThumbnailCache::Reset()
{
	// Brush destructors release the dynamic texture resources.
	Cache.Reset();
}

TSharedPtr<FSlateDynamicImageBrush> FDumpThumbnailCache::LoadThumbnail(const FDumpAssetEntry& Entry) const
{
	FPackageReader Reader;
	if (!Reader.OpenPackageFile(FStringView(Entry.GetPackageName()), FStringView(Entry.SourceFile)))
	{
		return nullptr;
	}

	// Enumerates the package's thumbnail table (names + file offsets) without loading anything.
	TArray<FObjectFullNameAndThumbnail> Thumbnails;
	if (!Reader.GetThumbnails(Thumbnails) || Thumbnails.Num() == 0)
	{
		return nullptr;
	}

	// Prefer the entry matching the asset (packages normally hold one), else take the first.
	const FString WantedSuffix = TEXT(".") + Entry.AssetName;
	const FObjectFullNameAndThumbnail* Chosen = &Thumbnails[0];
	for (const FObjectFullNameAndThumbnail& Candidate : Thumbnails)
	{
		if (Candidate.ObjectFullName.ToString().EndsWith(WantedSuffix))
		{
			Chosen = &Candidate;
			break;
		}
	}

	// FObjectThumbnail holds only ints and byte arrays, so it can serialize straight
	// through the already-open package reader — no second file open needed. The offset
	// comes from the file itself: validate it, or a corrupt table would trip the
	// fatal range check inside FArchive::Seek and take the whole editor down.
	if (Chosen->FileOffset <= 0 || static_cast<int64>(Chosen->FileOffset) >= Reader.TotalSize())
	{
		return nullptr;
	}
	Reader.Seek(Chosen->FileOffset);

	FObjectThumbnail Thumbnail;
	Thumbnail.Serialize(Reader);
	if (Reader.IsError() || Thumbnail.IsEmpty())
	{
		return nullptr;
	}

	// Thumbnails are stored compressed (PNG/JPEG); this decompresses to BGRA8 — exactly
	// what the Slate renderer expects.
	const TArray<uint8>& RawPixels = Thumbnail.GetUncompressedImageData();
	if (RawPixels.Num() == 0)
	{
		return nullptr;
	}

	const FName ResourceName(*(TEXT("AssetPullerThumb:") + Entry.SourceFile));
	return FSlateDynamicImageBrush::CreateWithImageData(ResourceName,
		FVector2D(Thumbnail.GetImageWidth(), Thumbnail.GetImageHeight()), RawPixels);
}
