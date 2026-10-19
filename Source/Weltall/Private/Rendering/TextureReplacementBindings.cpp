#include "TextureReplacementBindings.h"

#include "Rendering/TextureModLibrary.h"

namespace
{
	/** Bindings kept for regions no frame has drawn lately, past which the oldest are let go. */
	constexpr int32 KeptBindings = 16384;
	constexpr uint64 RecentFrames = 600;
	/** How long a frame that asked for new pictures waits for them: small ones arrive at once, large ones later. */
	constexpr double DecodeWait = 0.008;

	uint32 UnitsRevision(const Weltall::FVram& Vram, const Weltall::FTextureRegion& Region)
	{
		return Vram.RevisionOver(Weltall::TextureRegionUnits(Region));
	}

	uint32 ClutRevision(const Weltall::FVram& Vram, const Weltall::FTextureRegion& Region)
	{
		const Weltall::FTexturePage Page = Weltall::TextureViewPage(Region.View);
		if (Page.Depth == Weltall::ETextureDepth::Direct15) return 0;
		const Weltall::FClutPosition Clut = Weltall::TextureViewClut(Region.View);
		return Vram.RevisionOver({static_cast<int16>(Clut.X), static_cast<int16>(Clut.Y), static_cast<int16>(Page.Depth == Weltall::ETextureDepth::Indexed4 ? 16 : 256), 1});
	}

	TSharedPtr<const TArray<uint16>> ReadPalette(const Weltall::FVram& Vram, uint32 View)
	{
		std::array<uint16, 256> Colors{};
		const std::span<const uint16> Clut = Weltall::ReadTextureViewClut(Vram, View, Colors);
		if (Clut.empty()) return nullptr;
		return MakeShared<const TArray<uint16>>(Clut.data(), static_cast<int32>(Clut.size()));
	}
}

bool FTextureReplacementBindings::Follow(const Weltall::FVram& Vram, std::span<const Weltall::FTextureRegion> Regions,
	std::span<const Weltall::FTexturePictureSeed> ModelSeeds, TFunctionRef<bool(const Weltall::FTextureRegion&, bool)> IsExcluded)
{
	if (Generation != FTextureModLibrary::Generation())
	{
		Generation = FTextureModLibrary::Generation();
		Bindings.Empty();
		Pictures.Empty();
		Learned.Empty();
		++LearnedRevision;
	}
	++Frame;
	Next.Reset();
	FTextureModLibrary::CollectDecodes();
	const uint64 Started = FTextureModLibrary::DecodesStarted();
	const uint64 Taught = LearnedRevision;
	const bool bCapturing = FTextureModLibrary::IsCapturing();
	FollowPictures(Vram, ModelSeeds, bCapturing, IsExcluded);
	Drawn.Reset();
	for (const Weltall::FTextureRegion& Region : Regions)
	{
		if (Region.Width == 0 || Region.Height == 0 || IsExcluded(Region, false)) continue;
		const FRegionId Id{Vram.Lineage(), Region};
		FSetElementId Element = Bindings.FindId(Id);
		if (!Element.IsValidId())
		{
			Bindings.Add(Id);
			Element = Bindings.FindId(Id);
		}
		Drawn.Add(Element);
		FBinding& Binding = Bindings.Get(Element).Value;
		const uint32 Units = UnitsRevision(Vram, Region);
		const uint32 Clut = ClutRevision(Vram, Region);
		const bool bSteady = Binding.bRead && Binding.UnitsRevision == Units;
		if (!bSteady)
		{
			Binding.Texels = Weltall::ReadTextureRegionTexels(Vram, Region);
			Binding.Key = Weltall::TextureRegionKey(Vram, Region, Binding.Texels);
			Binding.UnitsRevision = Units;
			Binding.ClutRevision = Clut;
			Binding.bRead = true;
			Binding.bCapturePending = bCapturing;
			Bind(Binding, Vram, Region, false);
		}
		else if (Binding.ClutRevision != Clut)
		{
			Binding.ClutRevision = Clut;
			const uint64 Key = Weltall::TextureRegionKey(Vram, Region, Binding.Texels);
			if (Key != Binding.Key)
			{
				Binding.Key = Key;
				Bind(Binding, Vram, Region, true);
			}
		}
		else if (Binding.bPending || (!Binding.Image && Binding.LearnedSeen != LearnedRevision))
		{
			Bind(Binding, Vram, Region, true);
		}
		// Captured once its texels have held still for a frame: a picture the game animates by moving texels has none to save.
		if (bSteady && Binding.bCapturePending && (!FTextureModLibrary::NeedsCapture(Binding.Key) || FTextureModLibrary::HasCaptureRoom()))
		{
			Binding.bCapturePending = false;
			if (FTextureModLibrary::NeedsCapture(Binding.Key)) FTextureModLibrary::Capture(Binding.Key, Weltall::DecodeTextureRegion(Vram, Region));
		}
		Binding.LastFrame = Frame;
	}
	// Pictures asked for this frame that arrive within the wait are drawn this frame, and what this frame taught is
	// learned in it: the pictures VRAM holds and the regions that asked teach first, then the regions of the same indices.
	const bool bAsked = FTextureModLibrary::DecodesStarted() != Started;
	if (bAsked)
	{
		FTextureModLibrary::WaitForDecodes(DecodeWait);
		RetryPictures();
	}
	if (bAsked || LearnedRevision != Taught)
	{
		for (const bool bLearning : {false, true})
		{
			for (const FSetElementId Element : Drawn)
			{
				auto& [Id, Binding] = Bindings.Get(Element);
				if (bLearning ? !Binding.Image && Binding.LearnedSeen != LearnedRevision : bAsked && Binding.bPending) Bind(Binding, Vram, Id.Region, true);
			}
		}
	}
	for (const FSetElementId Element : Drawn)
	{
		const auto& [Id, Binding] = Bindings.Get(Element);
		if (Binding.Image) Next.Add({Id.Region, Binding.Image, Binding.Palette});
	}
	if (Bindings.Num() + Pictures.Num() > KeptBindings) Forget();
	if (Next == Current) return false;
	Swap(Next, Current);
	return true;
}

void FTextureReplacementBindings::Bind(FBinding& Binding, const Weltall::FVram& Vram, const Weltall::FTextureRegion& Region, bool bKeepOnMiss)
{
	Binding.LearnedSeen = LearnedRevision;
	const uint64 Indices = Weltall::AssetKeyOfDigest(Binding.Texels.Digest);
	// A whole page holds pictures the game draws through different CLUTs, so its indices say nothing of what another CLUT
	// shows: it neither teaches nor learns, and a pack's pictures in it are regions of their own (FollowTextureModSources).
	const bool bWholePage = Region == Weltall::WholeTextureView(Region.View);
	FTextureModLookup Lookup = FTextureModLibrary::Find(Binding.Key, Region.PictureTexelsWide(), Region.PictureTexelsHigh(), MaxPixels);
	if (!Lookup.Image && !Lookup.bPending && !bWholePage) Lookup = FTextureModLibrary::FindPacked(Vram, Region, Binding.Key, MaxPixels);
	Binding.bPending = Lookup.bPending;
	if (Lookup.Image)
	{
		Binding.Image = MoveTemp(Lookup.Image);
		Binding.Palette = ReadPalette(Vram, Region.View);
		// The same indices drawn through another CLUT (a fog level, a colour variant) take this picture, following their colours from these.
		if (Binding.Palette && !bWholePage) Teach(Indices, Binding.Image, Binding.Palette);
		return;
	}
	if (!bWholePage && !Weltall::ReadTextureViewClut(Vram, Region.View, Colors).empty())
	{
		if (const FLearned* Taught = Learned.Find(Indices))
		{
			Binding.Image = Taught->Image;
			Binding.Palette = Taught->Palette;
			return;
		}
	}
	if (!bKeepOnMiss)
	{
		Binding.Image.Reset();
		Binding.Palette.Reset();
	}
}

void FTextureReplacementBindings::Teach(uint64 Indices, const TSharedPtr<const FTextureModImage>& Image, const TSharedPtr<const TArray<uint16>>& Palette)
{
	FLearned& Taught = Learned.FindOrAdd(Indices);
	if (Taught.Image != Image || Taught.Palette != Palette)
	{
		Taught = {Image, Palette};
		++LearnedRevision;
	}
}

void FTextureReplacementBindings::FollowPictures(const Weltall::FVram& Vram, std::span<const Weltall::FTexturePictureSeed> ModelSeeds, bool bCapturing,
	TFunctionRef<bool(const Weltall::FTextureRegion&, bool)> IsExcluded)
{
	if (SeedsLineage != Vram.Lineage() || SeedsRevision != Vram.Revision())
	{
		SeedsLineage = Vram.Lineage();
		SeedsRevision = Vram.Revision();
		Seeds.clear();
		Weltall::AddPictureSeeds(Vram.Pictures(), Seeds);
	}
	for (const std::span<const Weltall::FTexturePictureSeed> Taught : {std::span<const Weltall::FTexturePictureSeed>(Seeds), ModelSeeds})
	{
		for (const Weltall::FTexturePictureSeed& Seed : Taught) FollowPicture(Vram, Seed, bCapturing, IsExcluded);
	}
}

void FTextureReplacementBindings::FollowPicture(const Weltall::FVram& Vram, const Weltall::FTexturePictureSeed& Seed, bool bCapturing,
	TFunctionRef<bool(const Weltall::FTextureRegion&, bool)> IsExcluded)
{
	const bool bIndexed = Weltall::TextureViewPage(Seed.Region.View).Depth != Weltall::ETextureDepth::Direct15;
	// An indexed picture that came without colours has none to teach or be saved in; its own colours lie nowhere in VRAM to be written over.
	if ((bIndexed && !Seed.Palette) || IsExcluded(Seed.Region, true)) return;
	FBinding& Picture = Pictures.FindOrAdd({Vram.Lineage(), Seed.Region});
	const uint32 Units = UnitsRevision(Vram, Seed.Region);
	const std::span<const uint16> Own = Seed.Palette ? std::span<const uint16>(*Seed.Palette) : std::span<const uint16>();
	const bool bSteady = Picture.bRead && Picture.UnitsRevision == Units;
	if (!bSteady)
	{
		Picture.Texels = Weltall::ReadTextureRegionTexels(Vram, Seed.Region);
		Picture.Key = Weltall::TextureRegionKeyWith(Picture.Texels, Own);
		Picture.Named = Seed.Key != 0 ? Seed.Key : Picture.Key;
		Picture.UnitsRevision = Units;
		Picture.bRead = true;
		Picture.bCapturePending = bCapturing;
		Picture.Palette.Reset();
		if (bIndexed) Picture.Palette = MakeShared<const TArray<uint16>>(Own.data(), static_cast<int32>(Own.size()));
		Picture.Image.Reset();
		FindPicture(Picture, Seed.Region);
	}
	else if (Seed.Key != 0 && Seed.Key != Picture.Named)
	{
		// Another model's picture on the same texels.
		Picture.Named = Seed.Key;
		Picture.Image.Reset();
		FindPicture(Picture, Seed.Region);
	}
	else if (Picture.bPending)
	{
		FindPicture(Picture, Seed.Region);
	}
	if (bSteady && Picture.bCapturePending && (!FTextureModLibrary::NeedsCapture(Picture.Key) || FTextureModLibrary::HasCaptureRoom()))
	{
		Picture.bCapturePending = false;
		if (FTextureModLibrary::NeedsCapture(Picture.Key)) FTextureModLibrary::Capture(Picture.Key, Weltall::DecodeTextureRegionWith(Vram, Seed.Region, Own));
	}
	Picture.LastFrame = Frame;
}

void FTextureReplacementBindings::FindPicture(FBinding& Picture, const Weltall::FTextureRegion& Region)
{
	FTextureModLookup Lookup = FTextureModLibrary::Find(Picture.Named, Region.PictureTexelsWide(), Region.PictureTexelsHigh(), MaxPixels);
	Picture.bPending = Lookup.bPending;
	if (!Lookup.Image) return;
	Picture.Image = MoveTemp(Lookup.Image);
	if (Picture.Palette) Teach(Weltall::AssetKeyOfDigest(Picture.Texels.Digest), Picture.Image, Picture.Palette);
}

void FTextureReplacementBindings::RetryPictures()
{
	for (auto& [Id, Picture] : Pictures)
	{
		if (Picture.bPending && Picture.LastFrame == Frame) FindPicture(Picture, Id.Region);
	}
}

void FTextureReplacementBindings::Forget()
{
	for (TMap<FRegionId, FBinding>* Kept : {&Bindings, &Pictures})
	{
		for (auto It = Kept->CreateIterator(); It; ++It)
		{
			if (It.Value().LastFrame + RecentFrames < Frame) It.RemoveCurrent();
		}
	}
}

void FTextureReplacementBindings::Reset()
{
	Bindings.Empty();
	Pictures.Empty();
	Seeds.clear();
	SeedsLineage = 0;
	Learned.Empty();
	++LearnedRevision;
	Current.Reset();
	Next.Reset();
}
