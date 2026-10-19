#include "Game/DiscTextureCatalogue.h"

#include "Battle/Show/BattleModelTextures.h"
#include "Battle/Show/BattleObjectFiles.h"
#include "Battle/Show/BattleSceneGraphics.h"
#include "Battle/Show/BattleShowHud.h"
#include "Battle/Show/BattleSpriteFiles.h"
#include "Battle/Show/BattleSpriteTextures.h"
#include "Battle/Show/BattleStageFiles.h"
#include "Field/FieldGears.h"
#include "Field/FieldMap.h"
#include "Field/FieldPartySprites.h"
#include "Field/FieldScene.h"
#include "Field/FieldSpriteFiles.h"
#include "Field/FieldSprites.h"
#include "Field/FieldTextureRegions.h"
#include "Formats/Lzss.h"
#include "Formats/SectionContainer.h"
#include "Formats/Tim.h"
#include "Gears/GearTextureRegions.h"
#include "Graphics/SpriteCells.h"
#include "Graphics/TexturePictures.h"
#include "Graphics/TextureRegion.h"
#include "Media/GameFileIndex.h"

#include <algorithm>
#include <cstdio>
#include <memory>

namespace Weltall
{
	namespace
	{
		constexpr int32 SpriteDirectory = 4;
		/** The party's field sprites, then their gears' (the last a copy of an earlier one). */
		constexpr int32 FirstPartyFile = FPartySpriteFiles::DiscFileOffset;
		constexpr int32 LastPartyFile = 0x25;
		constexpr int32 FirstExtraAnimationFile = 0x77A;
		constexpr int32 ExtraAnimationFiles = 121;
		/** Past this a file is a movie's or a sound bank's: none of the game's pictures lies in one. */
		constexpr int32 LargestPictureFile = 4 * 1024 * 1024;

		bool ShowsTexels(const FImageRgba8& Image)
		{
			for (std::size_t Alpha = 3; Alpha < Image.Pixels.size(); Alpha += 4)
			{
				if (Image.Pixels[Alpha] != 0)
				{
					return true;
				}
			}
			return false;
		}

		/** Every cell of every frame under every palette set, each read as soon as its frame has made its uploads. */
		template <typename TVisit>
		void ForEachCell(const FSpriteFile& File, const FSpriteFilePlace& Place, FVram& Vram, TVisit&& Visit)
		{
			for (int32 Set = 0; Set < SpritePaletteSets(File); ++Set)
			{
				ForEachSpriteFrame(File, Place, Set, Vram, [&Visit, &Vram, Set](std::span<const FSpritePart> Parts)
				{
					for (const FSpritePart& Part : Parts)
					{
						Visit(Vram, SpritePartCell(Part), Set);
					}
				});
			}
		}

		/** A sheet's name, for sprite file Number of a map ("sprite-03-cells-0") or the item's own file ("cells-0"). */
		std::string SheetName(int32 Number, int32 Set)
		{
			char Name[32];
			if (Number >= 0)
			{
				std::snprintf(Name, sizeof(Name), "sprite-%02d-cells-%d", Number, Set);
			}
			else
			{
				std::snprintf(Name, sizeof(Name), "cells-%d", Set);
			}
			return Name;
		}

		uint64 OnSheet(uint64 Key, const std::string& Sheet)
		{
			return Key ^ std::hash<std::string>{}(Sheet) * 0x9E3779B97F4A7C15ull;
		}
	}

	FDiscTextureCatalogue::FDiscTextureCatalogue(const FCdImage& InImage, const FGameFileIndex& InIndex)
		: Image(InImage), Index(InIndex)
	{
		for (int32 File = FirstPartyFile; File <= LastPartyFile; ++File)
		{
			Items.push_back({EItem::SpriteFile, 0, SpriteDirectory, File});
		}
		for (int32 File = FirstExtraAnimationFile; File < FirstExtraAnimationFile + ExtraAnimationFiles; ++File)
		{
			Items.push_back({EItem::SpriteFile, 0, SpriteDirectory, File});
		}
		for (int32 Map = 0; Map < FieldMapCount; ++Map)
		{
			Items.push_back({EItem::Map, Map});
		}
		const std::vector<FIndexRecord>& Records = Index.Records();
		for (std::size_t Record = 0; Record < Records.size(); ++Record)
		{
			if (!Records[Record].IsFile() || Records[Record].Size > LargestPictureFile)
			{
				continue;
			}
			const std::optional<FGameFileIndex::FFileName> Name = Index.NameOf(Record);
			Items.push_back({EItem::Pictures, static_cast<int32>(Record), Name ? Name->Directory : -1, Name ? Name->Number : 0});
		}
		const auto AddBattle = [this](EBattleModelSource Source, int32 Number, int32 Variant = 0)
		{
			Items.push_back({EItem::BattleModels, Number, static_cast<int32>(Source), Variant});
		};
		for (int32 Stage = 0; Stage < FBattleStageFiles::Count(Index); ++Stage)
		{
			AddBattle(EBattleModelSource::Stage, Stage);
		}
		for (const int32 Scene : FBattleSceneGraphics::Scenes(Index))
		{
			AddBattle(EBattleModelSource::SceneEnemies, Scene);
		}
		if (TResult<FBattleObjectTables> Tables = FBattleObjectTables::Load(Image, Index))
		{
			BattleTables = std::make_unique<FBattleObjectTables>(std::move(*Tables));
			for (int32 Gear = 0; Gear < FBattleObjectTables::GearCount; ++Gear)
			{
				for (int32 Variant = 0; Variant <= BattleTables->Gears[static_cast<std::size_t>(Gear)].LastVariant; ++Variant)
				{
					AddBattle(EBattleModelSource::Gear, Gear, Variant);
				}
			}
			for (int32 Special = 0; Special < BattleObjectFiles::SpecialModelCount; ++Special)
			{
				AddBattle(EBattleModelSource::Special, Special);
			}
		}
		if (TResult<FBattleSpriteFiles> Sprites = FBattleSpriteFiles::Load(Image, Index))
		{
			BattleSprites = std::make_unique<FBattleSpriteFiles>(std::move(*Sprites));
			for (int32 Character = 0; Character < FBattleSpriteFiles::CharacterCount; ++Character)
			{
				Items.push_back({EItem::BattleParty, Character});
			}
			for (int32 Effect = -1; Effect < BattleSprites->EffectCount(); ++Effect)
			{
				Items.push_back({EItem::BattleEffect, Effect});
			}
		}
		Items.push_back({EItem::BattleHud, 0});
		Items.push_back({EItem::BattleHud, 1});
		for (int32 GearModel = 0; GearModel < FGearFileNumbers::ModelCount; ++GearModel)
		{
			Items.push_back({EItem::FieldGear, GearModel});
		}
	}

	FDiscTextureCatalogue::~FDiscTextureCatalogue() = default;

	std::string FDiscTextureCatalogue::FolderOf(std::size_t Item) const
	{
		const FItem& Of = Items[Item];
		char Folder[32];
		switch (Of.Kind)
		{
		case EItem::SpriteFile:
			std::snprintf(Folder, sizeof(Folder), "sprites/%02d-%04d", Of.Directory, Of.File);
			break;
		case EItem::Map:
			std::snprintf(Folder, sizeof(Folder), "field/%04d", Of.Number);
			break;
		case EItem::Pictures:
			if (Of.Directory >= 0)
			{
				std::snprintf(Folder, sizeof(Folder), "files/%02d/%04d", Of.Directory, Of.File);
			}
			else
			{
				std::snprintf(Folder, sizeof(Folder), "records/%06d", Of.Number);
			}
			break;
		case EItem::BattleModels:
			switch (static_cast<EBattleModelSource>(Of.Directory))
			{
			case EBattleModelSource::Stage:
				std::snprintf(Folder, sizeof(Folder), "battle/stages/%03d", Of.Number);
				break;
			case EBattleModelSource::SceneEnemies:
				std::snprintf(Folder, sizeof(Folder), "battle/enemies/%03d", Of.Number);
				break;
			case EBattleModelSource::Gear:
				std::snprintf(Folder, sizeof(Folder), "battle/gears/%02d-%d", Of.Number, Of.File);
				break;
			case EBattleModelSource::Special:
				std::snprintf(Folder, sizeof(Folder), "battle/special/%d", Of.Number);
				break;
			}
			break;
		case EItem::BattleParty:
			std::snprintf(Folder, sizeof(Folder), "battle/party/%02d", Of.Number);
			break;
		case EItem::BattleEffect:
			if (Of.Number < 0)
			{
				std::snprintf(Folder, sizeof(Folder), "battle/effects/common");
			}
			else
			{
				std::snprintf(Folder, sizeof(Folder), "battle/effects/%03d", Of.Number);
			}
			break;
		case EItem::BattleHud:
			std::snprintf(Folder, sizeof(Folder), "%s", Of.Number == 0 ? "battle/hud" : "battle/result");
			break;
		case EItem::FieldGear:
			std::snprintf(Folder, sizeof(Folder), "gears/%02d", Of.Number);
			break;
		}
		return Folder;
	}

	TResult<std::vector<FDiscTexture>> FDiscTextureCatalogue::Step()
	{
		std::vector<FDiscTexture> Pictures;
		if (IsComplete())
		{
			return Pictures;
		}
		const FItem Item = Items[Next];
		PlacedInItem.clear();
		SeenOnSheets.clear();
		const std::optional<FError> Error = Item.Kind == EItem::Map ? AddMap(Item.Number, Pictures)
			: Item.Kind == EItem::SpriteFile ? AddSpriteFile(Item.Directory, Item.File, Pictures)
			: Item.Kind == EItem::BattleModels ? AddBattleModels(Item, Pictures)
			: Item.Kind == EItem::BattleParty ? AddBattleParty(Item.Number, Pictures)
			: Item.Kind == EItem::BattleEffect ? AddBattleEffect(Item.Number, Pictures)
			: Item.Kind == EItem::BattleHud ? AddBattleHud(Item.Number != 0, Pictures)
			: Item.Kind == EItem::FieldGear ? AddFieldGear(Item.Number, Pictures)
			: AddFilePictures(static_cast<std::size_t>(Item.Number), Pictures);
		++Next;
		if (Error)
		{
			return MakeError(FolderOf(Next - 1) + ": " + Error->Message);
		}
		return Pictures;
	}

	std::optional<FError> FDiscTextureCatalogue::AddMap(int32 MapNumber, std::vector<FDiscTexture>& Pictures)
	{
		const TResult<FFieldMap> Map = LoadFieldMap(Image, Index, MapNumber);
		if (!Map)
		{
			return Map.Error();
		}
		const TResult<FFieldScene> Scene = BuildFieldScene(*Map);
		if (!Scene)
		{
			return Scene.Error();
		}
		const auto Vram = std::make_unique<FVram>();
		UploadFieldTextures(*Map, *Vram, false);
		for (const FTextureRegion& Region : FieldSceneryTextureRegions(*Scene))
		{
			Add(*Vram, Region, Pictures);
		}
		for (const FTextureRegion& Region : FieldPanoramaTextureRegions(*Map))
		{
			Add(*Vram, Region, Pictures);
		}
		const TResult<std::vector<FSpriteFile>> Files = ParseFieldSpriteFiles(Map->SpriteSets);
		if (!Files)
		{
			return Files.Error();
		}
		const std::vector<FTextureRegion> Sheets = FieldSpriteSheetRegions(*Map);
		std::unique_ptr<FVram> Streamed;
		for (std::size_t Number = 0; Number < Files->size() && Number < Map->SpriteSlots.size(); ++Number)
		{
			const FSpriteFile& File = (*Files)[Number];
			const FSpriteFilePlace Place = FieldMapSpritePlace(Map->SpriteSlots[Number], static_cast<int32>(Number), 0);
			if (!File.IsResident(File.Tables()))
			{
				if (!Streamed)
				{
					Streamed = std::make_unique<FVram>();
				}
				ForEachCell(File, Place, *Streamed, [this, &Pictures, Number](const FVram& Cells, const FTextureRegion& Cell, int32 Set)
				{
					Add(Cells, Cell, Pictures, SheetName(static_cast<int32>(Number), Set));
				});
				continue;
			}
			// A sheet's colours are its palette set's, which each set's sprite uploads to its own place in the file's row.
			const int32 Colors = File.PalettesPerSet(File.Tables()) * 16;
			for (int32 Set = 0; Set < SpritePaletteSets(File); ++Set)
			{
				const FSpriteFilePlace SetPlace = FieldMapSpritePlace(Map->SpriteSlots[Number], static_cast<int32>(Number), Set);
				Vram->Upload(SpritePaletteUpload(File, SetPlace, Set));
				for (const FTextureRegion& Sheet : Sheets)
				{
					const FClutPosition Clut = TextureViewClut(Sheet.View);
					if (Clut.Y == SetPlace.ClutY && Clut.X >= SetPlace.ClutX && Clut.X < SetPlace.ClutX + Colors)
					{
						Add(*Vram, Sheet, Pictures);
					}
				}
			}
		}
		return std::nullopt;
	}

	std::optional<FError> FDiscTextureCatalogue::AddSpriteFile(int32 Directory, int32 Number, std::vector<FDiscTexture>& Pictures)
	{
		const TResult<FSpriteFile> File = ReadSpriteFile(Image, Index, Directory, Number);
		if (!File)
		{
			return File.Error();
		}
		// A resident one names places in textures a mode put in VRAM; only a streamed one carries its own texels.
		if (File->IsResident(File->Tables()))
		{
			return std::nullopt;
		}
		const auto Vram = std::make_unique<FVram>();
		ForEachCell(*File, FieldPartySpritePlace(), *Vram, [this, &Pictures](const FVram& Cells, const FTextureRegion& Cell, int32 Set)
		{
			Add(Cells, Cell, Pictures, SheetName(-1, Set));
		});
		return std::nullopt;
	}

	std::optional<FError> FDiscTextureCatalogue::AddFilePictures(std::size_t Record, std::vector<FDiscTexture>& Pictures)
	{
		const TResult<std::vector<uint8>> Bytes = ReadFileSectors(Image, Index.Records()[Record]);
		if (!Bytes)
		{
			return Bytes.Error();
		}
		const std::span<const uint8> File = std::span<const uint8>(*Bytes).first(static_cast<std::size_t>(Index.Records()[Record].Size));
		AddPictures(File, Pictures);
		// Most of the game's files are neither packed nor containers: one that does not unpack is simply not one.
		if (const TResult<std::vector<uint8>> Unpacked = UnpackLzss(*Bytes))
		{
			AddPictures(*Unpacked, Pictures);
		}
		if (const std::optional<uint32> Sections = ContainerSectionCount(File))
		{
			for (uint32 Number = 1; Number <= *Sections; ++Number)
			{
				if (const TResult<std::vector<uint8>> Section = UnpackContainerSection(*Bytes, Number))
				{
					AddPictures(*Section, Pictures);
				}
			}
		}
		return std::nullopt;
	}

	std::optional<FError> FDiscTextureCatalogue::AddBattleModels(const FItem& Item, std::vector<FDiscTexture>& Pictures)
	{
		const auto Vram = std::make_unique<FVram>();
		TResult<std::vector<FTexturePictureSeed>> Seeds = LoadBattleModelTextures(Image, Index, *BattleTables, static_cast<EBattleModelSource>(Item.Directory),
			Item.Number, Item.File, *Vram);
		if (!Seeds)
		{
			return Seeds.Error();
		}
		AddSeeds(*Vram, *Seeds, Pictures);
		if (static_cast<EBattleModelSource>(Item.Directory) != EBattleModelSource::SceneEnemies)
		{
			return std::nullopt;
		}
		const TResult<FBattleSceneGraphics> Scene = FBattleSceneGraphics::Load(Image, Index, Item.Number);
		if (!Scene)
		{
			return Scene.Error();
		}
		return ForEachBattleSceneSpriteCell(*Scene, [this, &Pictures](const FVram& Cells, const FTextureRegion& Cell, const std::string& Sheet)
		{
			Add(Cells, Cell, Pictures, Sheet);
		});
	}

	std::optional<FError> FDiscTextureCatalogue::AddBattleParty(int32 Character, std::vector<FDiscTexture>& Pictures)
	{
		return ForEachBattleCharacterCell(*BattleSprites, Character, [this, &Pictures](const FVram& Cells, const FTextureRegion& Cell, const std::string& Sheet)
		{
			Add(Cells, Cell, Pictures, Sheet);
		});
	}

	std::optional<FError> FDiscTextureCatalogue::AddBattleEffect(int32 Effect, std::vector<FDiscTexture>& Pictures)
	{
		const FBattleSpriteCellVisit Take = [this, &Pictures](const FVram& Cells, const FTextureRegion& Cell, const std::string& Sheet)
		{
			Add(Cells, Cell, Pictures, Sheet);
		};
		return Effect < 0 ? ForEachBattleCommonEffectCell(*BattleSprites, Take) : ForEachBattleEffectCell(*BattleSprites, Effect, Take);
	}

	std::optional<FError> FDiscTextureCatalogue::AddBattleHud(bool bResult, std::vector<FDiscTexture>& Pictures)
	{
		return ForEachBattleHudPicture(Image, Index, bResult, [this, &Pictures](const FVram& Vram, const FTextureRegion& Region) { Add(Vram, Region, Pictures); });
	}

	std::optional<FError> FDiscTextureCatalogue::AddFieldGear(int32 GearModel, std::vector<FDiscTexture>& Pictures)
	{
		const TResult<FGearFiles> Files = ReadGearFiles(Image, Index, GearModel);
		if (!Files)
		{
			return Files.Error();
		}
		// At the first gear's place, as a map's load puts it.
		const FGearTexturePlace Place = FieldGearTexturePlace(FFieldGears{}, 0);
		const auto Vram = std::make_unique<FVram>();
		AddSeeds(*Vram, UploadGearModelTextures(*Vram, Files->Model, &Place), Pictures);
		return std::nullopt;
	}

	void FDiscTextureCatalogue::AddSeeds(const FVram& Vram, std::span<const FTexturePictureSeed> Seeds, std::vector<FDiscTexture>& Pictures)
	{
		for (const FTexturePictureSeed& Seed : Seeds)
		{
			Add(Vram, Seed.Region, Pictures, {}, Seed.Palette ? std::optional<std::span<const uint16>>(*Seed.Palette) : std::nullopt);
		}
	}

	void FDiscTextureCatalogue::AddPictures(std::span<const uint8> Bytes, std::vector<FDiscTexture>& Pictures)
	{
		std::vector<FTexturePictureSeed> Seeds;
		for (const FTim& Tim : FindTims(Bytes))
		{
			if (!Tim.Image.Picture)
			{
				continue;
			}
			if (!PictureVram)
			{
				PictureVram = std::make_unique<FVram>();
			}
			PictureVram->Upload(Tim.Image);
			Seeds.clear();
			const FVramPicture Picture{Tim.Image.Rect, *Tim.Image.Picture};
			AddPictureSeeds(std::span<const FVramPicture>(&Picture, 1), Seeds);
			for (const FTexturePictureSeed& Seed : Seeds)
			{
				Add(*PictureVram, Seed.Region, Pictures, {}, Seed.Palette ? std::span<const uint16>(*Seed.Palette) : std::span<const uint16>());
			}
		}
	}

	void FDiscTextureCatalogue::Add(const FVram& Vram, const FTextureRegion& Region, std::vector<FDiscTexture>& Pictures, const std::string& Sheet,
		const std::optional<std::span<const uint16>>& Palette)
	{
		const uint64 Key = Palette ? TextureRegionKeyWith(ReadTextureRegionTexels(Vram, Region), *Palette) : TextureRegionKey(Vram, Region);
		const bool bOnSheet = !Sheet.empty();
		if (bOnSheet ? !SeenOnSheets.insert(OnSheet(Key, Sheet)).second : PlacedInItem.contains(Key))
		{
			return;
		}
		const bool bNew = !Seen.contains(Key);
		if (bNew || bOnSheet)
		{
			FImageRgba8 Picture = Palette ? DecodeTextureRegionWith(Vram, Region, *Palette) : DecodeTextureRegion(Vram, Region);
			if (!ShowsTexels(Picture))
			{
				return;
			}
			Seen.insert(Key);
			Pictures.push_back({Next, Key, std::move(Picture), Sheet});
		}
		if (PlacedInItem.insert(Key).second)
		{
			Found.push_back({Key, static_cast<uint32>(Next), static_cast<uint16>(Region.PictureTexelsWide()), static_cast<uint16>(Region.PictureTexelsHigh())});
		}
	}
}
