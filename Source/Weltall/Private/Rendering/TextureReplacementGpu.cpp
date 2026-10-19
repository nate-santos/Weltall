#include "TextureReplacementGpu.h"

#include "Rendering/TextureModLibrary.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RHITextureInitializer.h"
#include "RenderingThread.h"
#include "WeltallLog.h"

namespace
{
	constexpr uint32 EmptyView = 0xFFFFFFFF;
	constexpr uint32 MaxProbes = 8;
	constexpr uint32 MaxLookupSlots = 65536;
	constexpr int32 LookupWidth = 1024;
	constexpr int32 RegionsWidth = 1024;
	constexpr int32 PaletteWidth = 256;
	constexpr int32 MapTexels = 256;
	constexpr uint32 NoPalette = 0xFFFFFFFF;
	constexpr int32 FirstAtlasRows = 8;
	/** A region's entries: its picture's first tile, size, rectangle in the view and colours; then the whole picture's place in the view and size in texels. */
	constexpr int32 EntriesPerRegion = 2;

	/** The same mixing as the shaders' ReplacementHash. */
	uint32 HashView(uint32 Key)
	{
		Key ^= Key >> 16;
		Key *= 0x7FEB352D;
		Key ^= Key >> 15;
		Key *= 0x846CA68B;
		return Key ^ (Key >> 16);
	}

	/** Open addressing from each view's hash; a view that would need more probes than the shaders make grows the table. */
	TArray<FUintVector4> MakeLookup(const TArray<TPair<uint32, uint32>>& Views)
	{
		uint32 Capacity = 8;
		while (Capacity < static_cast<uint32>(Views.Num()) * 2) Capacity *= 2;
		for (;; Capacity *= 2)
		{
			TArray<FUintVector4> Lookup;
			Lookup.Init(FUintVector4(EmptyView, 0, 0, 0), Capacity);
			bool bFits = true;
			for (const auto& [View, Slot] : Views)
			{
				uint32 At = HashView(View) & (Capacity - 1);
				uint32 Probe = 0;
				for (; Probe < MaxProbes && Lookup[At].X != EmptyView; ++Probe) At = (At + 1) & (Capacity - 1);
				if (Probe == MaxProbes) { bFits = false; continue; }
				Lookup[At] = FUintVector4(View, Slot, 0, 0);
			}
			if (bFits || Capacity >= MaxLookupSlots)
			{
				if (!bFits) UE_LOG(LogWeltall, Warning, TEXT("Some texture views exceed the replacement lookup's collision limit and keep their original textures"));
				return Lookup;
			}
		}
	}

	FTextureRHIRef CreateEmpty(FRHICommandListBase& RHICmdList, const TCHAR* Name, FIntPoint Size, EPixelFormat Format)
	{
		return RHICmdList.CreateTexture(FRHITextureCreateDesc::Create2D(Name, Size, Format)
			.SetFlags(ETextureCreateFlags::ShaderResource).SetInitialState(ERHIAccess::SRVMask));
	}

	/** A texture of Width texels a row filled from Data, its last row padded with zeros. */
	FTextureRHIRef CreateFilled(FRHICommandListBase& RHICmdList, const TCHAR* Name, int32 Width, EPixelFormat Format, const void* Data, int32 Count,
		int32 BytesPerTexel)
	{
		const int32 Rows = FMath::Max(1, FMath::DivideAndRoundUp(Count, Width));
		const auto Desc = FRHITextureCreateDesc::Create2D(Name, FIntPoint(Width, Rows), Format)
			.SetFlags(ETextureCreateFlags::ShaderResource).SetInitialState(ERHIAccess::SRVMask).SetInitActionInitializer();
		FRHITextureInitializer Initializer = RHICmdList.CreateTextureInitializer(Desc);
		const auto Target = Initializer.GetTexture2DSubresource(0);
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			uint8* Into = static_cast<uint8*>(Target.Data) + static_cast<uint64>(Row) * Target.Stride;
			const int32 Filled = FMath::Clamp(Count - Row * Width, 0, Width);
			FMemory::Memzero(Into, static_cast<SIZE_T>(Width) * BytesPerTexel);
			if (Filled > 0) FMemory::Memcpy(Into, static_cast<const uint8*>(Data) + static_cast<uint64>(Row) * Width * BytesPerTexel, static_cast<SIZE_T>(Filled) * BytesPerTexel);
		}
		return Initializer.Finalize();
	}

	/** A picture's tiles, a row of the picture at a time as far as the atlas's rows let one copy take them. */
	void UploadPicture(FRHICommandListImmediate& RHICmdList, FRHITexture* Atlas, const FTextureModImage& Image, uint32 FirstTile)
	{
		const int32 Size = FTextureReplacementGpu::TileSize;
		const int32 Columns = FTextureReplacementGpu::AtlasColumns;
		const int32 Across = FMath::DivideAndRoundUp(Image.Width, Size);
		const int32 Down = FMath::DivideAndRoundUp(Image.Height, Size);
		for (int32 Y = 0; Y < Down; ++Y)
		{
			const int32 Height = FMath::Min(Size, Image.Height - Y * Size);
			for (int32 X = 0; X < Across;)
			{
				const uint32 Tile = FirstTile + static_cast<uint32>(Y * Across + X);
				const int32 Run = FMath::Min(Across - X, Columns - static_cast<int32>(Tile % Columns));
				const int32 Width = FMath::Min(Run * Size, Image.Width - X * Size);
				const FUpdateTextureRegion2D Region(Tile % Columns * Size, Tile / Columns * Size, 0, 0, Width, Height);
				const uint8* Source = Image.Rgba.GetData() + (static_cast<int64>(Y * Size) * Image.Width + X * Size) * 4;
				RHICmdList.UpdateTexture2D(Atlas, 0, Region, Image.Width * 4, Source);
				X += Run;
			}
		}
	}

	/** An atlas Rows tiles tall holding what Old held. */
	FTextureRHIRef GrowAtlas(FRHICommandListImmediate& RHICmdList, FRHITexture* Old, int32 OldRows, int32 Rows)
	{
		const int32 Width = FTextureReplacementGpu::AtlasColumns * FTextureReplacementGpu::TileSize;
		FTextureRHIRef Atlas = CreateEmpty(RHICmdList, TEXT("TextureReplacementAtlas"), FIntPoint(Width, Rows * FTextureReplacementGpu::TileSize), PF_R8G8B8A8);
		if (OldRows == 0) return Atlas;
		RHICmdList.Transition({FRHITransitionInfo(Old, ERHIAccess::SRVMask, ERHIAccess::CopySrc), FRHITransitionInfo(Atlas, ERHIAccess::SRVMask, ERHIAccess::CopyDest)});
		FRHICopyTextureInfo Copy;
		Copy.Size = FIntVector(Width, OldRows * FTextureReplacementGpu::TileSize, 1);
		RHICmdList.CopyTexture(Old, Atlas, Copy);
		RHICmdList.Transition({FRHITransitionInfo(Old, ERHIAccess::CopySrc, ERHIAccess::SRVMask), FRHITransitionInfo(Atlas, ERHIAccess::CopyDest, ERHIAccess::SRVMask)});
		return Atlas;
	}

	/** The most rows of tiles the atlas grows to: 512 MiB, or what the device's largest texture holds. */
	int32 MostAtlasRows()
	{
		return FMath::Min(FTextureReplacementGpu::MaxAtlasRows, static_cast<int32>(GMaxTextureDimensions) / FTextureReplacementGpu::TileSize);
	}
}

void FTextureReplacementGpu::InitRHI(FRHICommandListBase& RHICmdList)
{
	const uint32 Zero = 0;
	const FUintVector4 Missing(EmptyView, 0, 0, 0);
	Gpu.Views = CreateFilled(RHICmdList, TEXT("TextureReplacementViews"), 1, PF_R32G32B32A32_UINT, &Missing, 1, sizeof(FUintVector4));
	Gpu.RegionMaps = CreateFilled(RHICmdList, TEXT("TextureReplacementMaps"), 1, PF_R16_UINT, &Zero, 1, sizeof(uint16));
	Gpu.Regions = CreateFilled(RHICmdList, TEXT("TextureReplacementRegions"), 1, PF_R32G32B32A32_UINT, &Missing, 1, sizeof(FUintVector4));
	Gpu.Palettes = CreateFilled(RHICmdList, TEXT("TextureReplacementPalettes"), 1, PF_R16_UINT, &Zero, 1, sizeof(uint16));
	Gpu.Atlas = CreateFilled(RHICmdList, TEXT("TextureReplacementAtlas"), 1, PF_R8G8B8A8, &Zero, 1, sizeof(uint32));
	Gpu.ViewMask = 0;
	Gpu.ViewCount = 0;
}

void FTextureReplacementGpu::ReleaseRHI()
{
	Gpu = FTextureReplacementResources{};
}

bool FTextureReplacementGpu::AllocateTiles(uint32 Count, uint32& First)
{
	for (int32 Run = 0; Run < FreeRuns.Num(); ++Run)
	{
		auto& [Start, Free] = FreeRuns[Run];
		if (Free < Count) continue;
		First = Start;
		Start += Count;
		Free -= Count;
		if (Free == 0) FreeRuns.RemoveAt(Run);
		return true;
	}
	return false;
}

void FTextureReplacementGpu::FreeTiles(uint32 First, uint32 Count)
{
	int32 At = 0;
	while (At < FreeRuns.Num() && FreeRuns[At].Key < First) ++At;
	FreeRuns.Insert({First, Count}, At);
	if (At + 1 < FreeRuns.Num() && FreeRuns[At].Key + FreeRuns[At].Value == FreeRuns[At + 1].Key)
	{
		FreeRuns[At].Value += FreeRuns[At + 1].Value;
		FreeRuns.RemoveAt(At + 1);
	}
	if (At > 0 && FreeRuns[At - 1].Key + FreeRuns[At - 1].Value == FreeRuns[At].Key)
	{
		FreeRuns[At - 1].Value += FreeRuns[At].Value;
		FreeRuns.RemoveAt(At);
	}
}

FTextureReplacementGpu::FResidentPicture* FTextureReplacementGpu::MakeResident(const TSharedPtr<const FTextureModImage>& Image, FUploads& Uploads)
{
	if (FResidentPicture* Found = Resident.Find(Image.Get())) return Found;
	const uint32 Count = static_cast<uint32>(FMath::DivideAndRoundUp(Image->Width, TileSize) * FMath::DivideAndRoundUp(Image->Height, TileSize));
	uint32 First = 0;
	while (!AllocateTiles(Count, First))
	{
		const FTextureModImage* Oldest = nullptr;
		uint64 OldestFrame = Frame;
		for (const auto& [Key, Picture] : Resident)
		{
			if (Picture.LastFrame < OldestFrame) { Oldest = Key; OldestFrame = Picture.LastFrame; }
		}
		if (Oldest != nullptr)
		{
			const FResidentPicture Evicted = Resident.FindAndRemoveChecked(Oldest);
			FreeTiles(Evicted.FirstTile, Evicted.Tiles);
			continue;
		}
		if (AtlasRows < MostAtlasRows())
		{
			const int32 Rows = AtlasRows == 0 ? FirstAtlasRows : FMath::Min(AtlasRows * 2, MostAtlasRows());
			FreeTiles(static_cast<uint32>(AtlasRows * AtlasColumns), static_cast<uint32>((Rows - AtlasRows) * AtlasColumns));
			AtlasRows = Rows;
			Uploads.bNewAtlas = true;
			continue;
		}
		if (!bWarnedBudget)
		{
			UE_LOG(LogWeltall, Warning, TEXT("Texture replacements exceed %lld MiB for this scene; the rest keep their original textures"),
				static_cast<int64>(AtlasRows) * AtlasColumns * TileSize * TileSize * 4 / (1024 * 1024));
		}
		bWarnedBudget = true;
		return nullptr;
	}
	Uploads.Pictures.Add({Image, First});
	return &Resident.Add(Image.Get(), {Image, First, Count, Frame});
}

uint32 FTextureReplacementGpu::RegionId(const FBoundTextureRegion& Bound, const FResidentPicture& Picture, bool& bChanged)
{
	const FRegionKey Key{Bound.Region, Bound.Image.Get(), Bound.Palette.Get()};
	if (const uint32* Found = RegionIds.Find(Key))
	{
		FUintVector4& Entry = RegionEntries[static_cast<int32>(*Found) * EntriesPerRegion];
		if (Entry.X == Picture.FirstTile) return *Found;
		Entry.X = Picture.FirstTile;
		bChanged = true;
		return *Found;
	}
	const uint32 Id = FreeRegionIds.Num() > 0 ? FreeRegionIds.Pop(EAllowShrinking::No) : static_cast<uint32>(RegionLastFrame.Num());
	if (Id == static_cast<uint32>(RegionLastFrame.Num()))
	{
		RegionEntries.AddZeroed(EntriesPerRegion);
		PaletteRows.AddZeroed(PaletteWidth);
		RegionLastFrame.Add(0);
	}
	const Weltall::FTextureRegion& Region = Bound.Region;
	const uint32 Size = static_cast<uint32>(Bound.Image->Width) | static_cast<uint32>(Bound.Image->Height) << 16;
	const uint32 Rect = Region.U | Region.V << 8 | static_cast<uint32>(Region.Width - 1) << 16 | static_cast<uint32>(Region.Height - 1) << 24;
	const uint32 PictureFirst = static_cast<uint16>(Region.PictureFirstU()) | static_cast<uint32>(static_cast<uint16>(Region.PictureFirstV())) << 16;
	const uint32 PictureTexels = static_cast<uint32>(Region.PictureTexelsWide()) | static_cast<uint32>(Region.PictureTexelsHigh()) << 16;
	RegionEntries[static_cast<int32>(Id) * EntriesPerRegion] = FUintVector4(Picture.FirstTile, Size, Rect, Bound.Palette ? Id : NoPalette);
	RegionEntries[static_cast<int32>(Id) * EntriesPerRegion + 1] = FUintVector4(PictureFirst, PictureTexels, Bound.Image->bKindsFromAlpha ? 1u : 0u, 0);
	if (Bound.Palette)
	{
		FMemory::Memcpy(&PaletteRows[static_cast<int32>(Id) * PaletteWidth], Bound.Palette->GetData(), Bound.Palette->Num() * sizeof(uint16));
	}
	RegionIds.Add(Key, Id);
	bChanged = true;
	return Id;
}

void FTextureReplacementGpu::Update(TConstArrayView<FBoundTextureRegion> Bound)
{
	++Frame;
	const TSharedRef<FUploads> Uploads = MakeShared<FUploads>();
	Uploads->OldAtlasRows = AtlasRows;
	// Every picture this frame draws stays: room for a new one is made from the others.
	for (const FBoundTextureRegion& Entry : Bound)
	{
		if (FResidentPicture* Picture = Resident.Find(Entry.Image.Get())) Picture->LastFrame = Frame;
	}
	bool bRegionsChanged = false;
	TSet<uint32> NewIds;
	TMap<uint32, TArray<uint32>> ViewRegions;
	for (const FBoundTextureRegion& Entry : Bound)
	{
		const FResidentPicture* Picture = MakeResident(Entry.Image, *Uploads);
		if (Picture == nullptr) continue;
		bool bNew = false;
		const uint32 Id = RegionId(Entry, *Picture, bNew);
		if (bNew) NewIds.Add(Id);
		bRegionsChanged |= bNew;
		RegionLastFrame[static_cast<int32>(Id)] = Frame;
		ViewRegions.FindOrAdd(Entry.Region.View).AddUnique(Id);
	}
	for (auto It = RegionIds.CreateIterator(); It; ++It)
	{
		if (RegionLastFrame[static_cast<int32>(It.Value())] == Frame) continue;
		FreeRegionIds.Add(It.Value());
		It.RemoveCurrent();
		bRegionsChanged = true;
	}
	bool bViewsChanged = false;
	for (auto It = Slots.CreateIterator(); It; ++It)
	{
		if (ViewRegions.Contains(It.Key())) continue;
		FreeSlots.Add(It.Value().Number);
		It.RemoveCurrent();
		bViewsChanged = true;
	}
	for (const auto& [View, Ids] : ViewRegions)
	{
		FSlot* Slot = Slots.Find(View);
		bool bRemake = Slot == nullptr;
		if (Slot == nullptr)
		{
			Slot = &Slots.Add(View);
			Slot->Number = FreeSlots.Num() > 0 ? FreeSlots.Pop(EAllowShrinking::No) : SlotCount++;
			bViewsChanged = true;
		}
		bRemake = bRemake || Slot->Regions != Ids || Ids.ContainsByPredicate([&NewIds](uint32 Id) { return NewIds.Contains(Id); });
		if (!bRemake) continue;
		Slot->Regions = Ids;
		Slot->Map.Init(0, MapTexels * MapTexels);
		for (const uint32 Id : Ids)
		{
			const uint32 Rect = RegionEntries[static_cast<int32>(Id) * EntriesPerRegion].Z;
			const int32 FirstU = Rect & 0xFF, FirstV = Rect >> 8 & 0xFF, Width = (Rect >> 16 & 0xFF) + 1, Height = (Rect >> 24) + 1;
			for (int32 V = FirstV; V < FirstV + Height; ++V)
			{
				uint16* Row = &Slot->Map[V * MapTexels];
				for (int32 U = FirstU; U < FirstU + Width; ++U)
				{
					if (Row[U] == 0) Row[U] = static_cast<uint16>(Id + 1);
				}
			}
		}
		Uploads->Maps.Add({Slot->Number, Slot->Map});
	}
	const int32 NeededMapRows = FMath::DivideAndRoundUp(SlotCount, MapsAcross);
	if (NeededMapRows > MapRows)
	{
		MapRows = FMath::Max(NeededMapRows, MapRows * 2);
		Uploads->bNewMaps = true;
		Uploads->Maps.Reset();
		for (const auto& [View, Slot] : Slots) Uploads->Maps.Add({Slot.Number, Slot.Map});
	}
	Uploads->AtlasRows = AtlasRows;
	Uploads->MapRows = MapRows;
	if (bRegionsChanged)
	{
		Uploads->Regions = RegionEntries;
		Uploads->Palettes = PaletteRows;
	}
	if (bViewsChanged)
	{
		TArray<TPair<uint32, uint32>> Views;
		for (const auto& [View, Slot] : Slots) Views.Add({View, static_cast<uint32>(Slot.Number)});
		Uploads->Views = MakeLookup(Views);
		Uploads->ViewCount = static_cast<uint32>(Views.Num());
		Uploads->bViews = true;
	}
	Send(Uploads);
}

void FTextureReplacementGpu::Send(TSharedRef<FUploads> Uploads)
{
	ENQUEUE_RENDER_COMMAND(UploadTextureReplacements)([this, Uploads](FRHICommandListImmediate& RHICmdList)
	{
		if (Uploads->bNewAtlas)
		{
			Gpu.Atlas = GrowAtlas(RHICmdList, Gpu.Atlas, Uploads->OldAtlasRows, Uploads->AtlasRows);
		}
		for (const auto& [Image, FirstTile] : Uploads->Pictures) UploadPicture(RHICmdList, Gpu.Atlas, *Image, FirstTile);
		if (Uploads->bNewMaps)
		{
			Gpu.RegionMaps = CreateEmpty(RHICmdList, TEXT("TextureReplacementMaps"), FIntPoint(MapsAcross * MapTexels, Uploads->MapRows * MapTexels), PF_R16_UINT);
		}
		for (const auto& [Slot, Map] : Uploads->Maps)
		{
			const FUpdateTextureRegion2D Region(Slot % MapsAcross * MapTexels, Slot / MapsAcross * MapTexels, 0, 0, MapTexels, MapTexels);
			RHICmdList.UpdateTexture2D(Gpu.RegionMaps, 0, Region, MapTexels * sizeof(uint16), reinterpret_cast<const uint8*>(Map.GetData()));
		}
		if (!Uploads->Regions.IsEmpty())
		{
			Gpu.Regions = CreateFilled(RHICmdList, TEXT("TextureReplacementRegions"), RegionsWidth, PF_R32G32B32A32_UINT, Uploads->Regions.GetData(),
				Uploads->Regions.Num(), sizeof(FUintVector4));
			Gpu.Palettes = CreateFilled(RHICmdList, TEXT("TextureReplacementPalettes"), PaletteWidth, PF_R16_UINT, Uploads->Palettes.GetData(),
				Uploads->Palettes.Num(), sizeof(uint16));
		}
		if (Uploads->bViews)
		{
			const int32 Width = FMath::Min(Uploads->Views.Num(), LookupWidth);
			Gpu.Views = CreateFilled(RHICmdList, TEXT("TextureReplacementViews"), Width, PF_R32G32B32A32_UINT, Uploads->Views.GetData(), Uploads->Views.Num(),
				sizeof(FUintVector4));
			Gpu.ViewMask = static_cast<uint32>(Uploads->Views.Num() - 1);
			Gpu.ViewCount = Uploads->ViewCount;
		}
	});
}
