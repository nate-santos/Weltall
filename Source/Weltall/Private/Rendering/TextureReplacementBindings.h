#pragma once

#include "CoreMinimal.h"
#include "Graphics/TexturePictures.h"
#include "Graphics/TextureRegion.h"
#include "Graphics/Vram.h"

#include <array>
#include <span>
#include <vector>

struct FTextureModImage;

/** A region a frame draws with a replacement: its picture, and the colours its CLUT held when the two were matched. */
struct FBoundTextureRegion
{
	Weltall::FTextureRegion Region;
	TSharedPtr<const FTextureModImage> Image;
	/** Empty for a direct page. */
	TSharedPtr<const TArray<uint16>> Palette;

	bool operator==(const FBoundTextureRegion& Other) const
	{
		return Region == Other.Region && Image == Other.Image && Palette == Other.Palette;
	}
};

/**
 * Which replacement each region a frame draws stands for: a replacement of its own key, else the picture Beetle PSX HW
 * texture packs give the uploads under it (FTextureModLibrary::FindPacked), else what was learned (below). A region's
 * texels are read again only when VRAM under them changes; its CLUT's colours whenever they change. A region keeps its replacement while only its colours change (a
 * fade, palette animation), unless another replacement stands for the new colours: the drawing then follows the
 * colours from the matched ones. A region of the same indices as one that has matched a replacement (the same picture
 * through another CLUT: a fog level, a colour variant) takes that replacement, following its colours from the matched
 * ones, unless one stands for its own colours. The pictures VRAM holds teach their own colours the same way, drawn or
 * not, so that a picture drawn through colours the game made of its own takes the replacement made for it. A model's
 * picture finds its replacement by its key as the model's load left it (FTexturePictureSeed::Key) and teaches it to
 * whatever texels the model's own animations write over it, so the replacement stays on through them. A
 * replacement being read is drawn once it arrives, in the frame that asked for it when it arrives within a short wait;
 * until then the region draws its original, or the replacement whose colours it was following. A new picture is captured once its texels have stayed the same for a
 * frame. Game thread.
 */
class FTextureReplacementBindings
{
public:
	/**
	 * Binds the frame's regions, in order of precedence, skipping the excluded: IsExcluded is asked of each region, and of
	 * each picture VRAM holds and each of ModelSeeds with true for colours of its own rather than its view's CLUT. The
	 * seeds teach as the pictures do. True when the bound set changed.
	 */
	bool Follow(const Weltall::FVram& Vram, std::span<const Weltall::FTextureRegion> Regions, std::span<const Weltall::FTexturePictureSeed> ModelSeeds,
		TFunctionRef<bool(const Weltall::FTextureRegion&, bool)> IsExcluded);
	const TArray<FBoundTextureRegion>& Bound() const { return Current; }
	/** Forgets every binding: nothing is bound until the next Follow. */
	void Reset();

	/** The largest picture bound, in pixels: what the GPU's atlas holds at most. */
	static constexpr int64 MaxPixels = 8192LL * 8192;

private:
	struct FRegionId
	{
		uint32 Lineage = 0;
		Weltall::FTextureRegion Region;

		bool operator==(const FRegionId&) const = default;
		friend uint32 GetTypeHash(const FRegionId& Id)
		{
			uint32 Hash = HashCombineFast(Id.Lineage, Id.Region.View);
			Hash = HashCombineFast(Hash, static_cast<uint32>(Id.Region.U | Id.Region.V << 8));
			return HashCombineFast(Hash, static_cast<uint32>(Id.Region.Width | Id.Region.Height << 16));
		}
	};

	struct FBinding
	{
		Weltall::FTextureRegionTexels Texels;
		uint64 Key = 0;
		/** A picture's: the key its replacement is found by, a model's as its load left it, else Key. */
		uint64 Named = 0;
		uint32 UnitsRevision = 0;
		uint32 ClutRevision = 0;
		uint64 LastFrame = 0;
		/** The revision of what has been learned when it was last bound. */
		uint64 LearnedSeen = 0;
		bool bRead = false;
		bool bCapturePending = false;
		/** Its replacement is being read. */
		bool bPending = false;
		TSharedPtr<const FTextureModImage> Image;
		TSharedPtr<const TArray<uint16>> Palette;
	};

	/** A replacement an index image matched, with the colours it matched through. */
	struct FLearned
	{
		TSharedPtr<const FTextureModImage> Image;
		TSharedPtr<const TArray<uint16>> Palette;
	};

	void Bind(FBinding& Binding, const Weltall::FVram& Vram, const Weltall::FTextureRegion& Region, bool bKeepOnMiss);
	void Teach(uint64 Indices, const TSharedPtr<const FTextureModImage>& Image, const TSharedPtr<const TArray<uint16>>& Palette);
	void FollowPictures(const Weltall::FVram& Vram, std::span<const Weltall::FTexturePictureSeed> ModelSeeds, bool bCapturing,
		TFunctionRef<bool(const Weltall::FTextureRegion&, bool)> IsExcluded);
	void FollowPicture(const Weltall::FVram& Vram, const Weltall::FTexturePictureSeed& Seed, bool bCapturing,
		TFunctionRef<bool(const Weltall::FTextureRegion&, bool)> IsExcluded);
	/** Takes the picture's replacement when there is one; it teaches its colours. */
	void FindPicture(FBinding& Picture, const Weltall::FTextureRegion& Region);
	/** Asks again for this frame's pictures whose replacements were being read. */
	void RetryPictures();
	void Forget();

	TMap<FRegionId, FBinding> Bindings;
	/** The bindings of the regions this frame draws, in precedence. */
	TArray<FSetElementId> Drawn;
	/** The pictures' parts VRAM holds, by where they lie, and the colours each came with. */
	TMap<FRegionId, FBinding> Pictures;
	std::vector<Weltall::FTexturePictureSeed> Seeds;
	uint32 SeedsLineage = 0;
	uint32 SeedsRevision = 0;
	/** By the key of an index image's digest. */
	TMap<uint64, FLearned> Learned;
	uint64 LearnedRevision = 1;
	std::array<uint16, 256> Colors{};
	TArray<FBoundTextureRegion> Current;
	TArray<FBoundTextureRegion> Next;
	uint64 Frame = 0;
	uint64 Generation = MAX_uint64;
};
