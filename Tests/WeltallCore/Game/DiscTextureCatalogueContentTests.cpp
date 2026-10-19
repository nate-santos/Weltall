#include "Battle/Show/BattleSpriteFiles.h"
#include "Field/FieldMap.h"
#include "Field/FieldPresenter.h"
#include "Field/FieldTextureRegions.h"
#include "Formats/Tim.h"
#include "Game/DiscTextureCatalogue.h"
#include "Gears/GearFiles.h"
#include "Graphics/TexturePictures.h"
#include "Media/GameFileIndex.h"
#include "Media/PsxExecutable.h"
#include "Support/FieldPlaySession.h"
#include "Support/GameDiscs.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <set>

using namespace Weltall;

namespace
{
	/** The keys the catalogue found in an item, stepping it from the start. */
	std::set<uint64> CatalogueKeys(FDiscTextureCatalogue& Catalogue, std::size_t Item)
	{
		while (Catalogue.CompletedItems() <= Item)
		{
			const auto Pictures = Catalogue.Step();
			REQUIRE(Pictures);
		}
		std::set<uint64> Keys;
		for (const FDiscTexturePlace& Place : Catalogue.Places())
		{
			if (Place.Item == Item)
			{
				Keys.insert(Place.Key);
			}
		}
		return Keys;
	}
}

TEST_CASE("The texture export names Lahan's scenery and sprite sheets as the field draws them", "[Content][Game][TextureRegion]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const auto Session = Testing::EnterFieldMap(*Disc, 2);
	Session->Run(4);
	const FFieldPresenter& Drawing = Session->Play->Drawing();
	FDiscTextureCatalogue Catalogue(Disc->Image(), *Session->Index);
	constexpr std::size_t Lahan = 154 + 2;
	REQUIRE(Catalogue.FolderOf(Lahan) == "field/0002");
	const std::set<uint64> Exported = CatalogueKeys(Catalogue, Lahan);
	const std::vector<FTextureRegion> Scenery = FieldSceneryTextureRegions(Drawing.Scene());
	REQUIRE(Scenery.size() == 30);
	for (const FTextureRegion& Region : Scenery)
	{
		CHECK(Exported.contains(TextureRegionKey(Drawing.Vram(), Region)));
	}
	// Every sprite the map has made shows a sheet the export saved.
	REQUIRE(Drawing.SpriteSheets().size() == 10);
	int32 Drawn = 0;
	for (const FTextureRegion& Sheet : Drawing.SpriteSheets())
	{
		Drawn += Exported.contains(TextureRegionKey(Drawing.Vram(), Sheet)) ? 1 : 0;
	}
	CHECK(Drawn >= 5);
}

TEST_CASE("The texture export walks every map and the field's streamed sprite files", "[Content][Game][TextureRegion]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	auto Executable = ReadBootExecutable(*Disc);
	REQUIRE(Executable);
	auto Index = FGameFileIndex::Read(*Executable);
	REQUIRE(Index);
	FDiscTextureCatalogue Catalogue(Disc->Image(), *Index);
	const std::size_t Files = Catalogue.TotalItems() - (33 + 121 + FieldMapCount);
	CHECK(Files > 3000);
	CHECK(Catalogue.FolderOf(0) == "sprites/04-0005");
	CHECK(Catalogue.FolderOf(153) == "sprites/04-2034");
	CHECK(Catalogue.FolderOf(33 + 121 + FieldMapCount - 1) == "field/0729");
	CHECK(Catalogue.FolderOf(33 + 121 + FieldMapCount).starts_with("files/"));
	// The party's battle sprites, the effects and the HUD follow Battle's models; the field's gear models come last, the
	// gear shop's among them.
	const std::size_t FieldGears = Catalogue.TotalItems() - FGearFileNumbers::ModelCount;
	constexpr std::size_t Hud = 2;
	constexpr std::size_t Effects = 251 + 1;
	const std::size_t Party = FieldGears - Hud - Effects - FBattleSpriteFiles::CharacterCount;
	CHECK(Catalogue.FolderOf(Party - 1) == "battle/special/2");
	CHECK(Catalogue.FolderOf(Party) == "battle/party/00");
	CHECK(Catalogue.FolderOf(FieldGears - Hud - Effects - 1) == "battle/party/10");
	CHECK(Catalogue.FolderOf(FieldGears - Hud - Effects) == "battle/effects/common");
	CHECK(Catalogue.FolderOf(FieldGears - Hud - 1) == "battle/effects/250");
	CHECK(Catalogue.FolderOf(FieldGears - Hud) == "battle/hud");
	CHECK(Catalogue.FolderOf(FieldGears - 1) == "battle/result");
	CHECK(Catalogue.FolderOf(FieldGears) == "gears/00");
	CHECK(Catalogue.FolderOf(Catalogue.TotalItems() - 1) == "gears/71");
	// Fei's field sprites: streamed cells, each a picture of its own, under its one palette set.
	const auto Fei = Catalogue.Step();
	REQUIRE(Fei);
	CHECK(Fei->size() > 100);
	for (const FDiscTexture& Picture : *Fei)
	{
		CHECK(Picture.Item == 0);
		CHECK(Picture.Image.Width <= 64);
	}
}

TEST_CASE("The texture export saves every file's TIM pictures as the field holds its own", "[Content][Game][TextureRegion]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const auto Session = Testing::EnterFieldMap(*Disc, 2);
	Session->Run(4);
	FDiscTextureCatalogue Catalogue(Disc->Image(), *Session->Index);
	std::size_t Errors = 0;
	std::size_t SystemImages = 0;
	std::size_t MenuImages = 0;
	while (!Catalogue.IsComplete())
	{
		const std::size_t Item = Catalogue.CompletedItems();
		const auto Pictures = Catalogue.Step();
		Errors += Pictures ? 0 : 1;
		if (Catalogue.FolderOf(Item) == "files/04/0167")
		{
			REQUIRE(Pictures);
			SystemImages = Pictures->size();
		}
		// The Menu's data, a container whose sections hold its TIMs.
		if (Catalogue.FolderOf(Item) == "files/16/0001")
		{
			REQUIRE(Pictures);
			MenuImages = Pictures->size();
		}
	}
	CHECK(Errors == 0);
	CHECK(SystemImages == 8);
	CHECK(MenuImages > 40);
	std::set<uint64> Exported;
	for (const FDiscTexturePlace& Place : Catalogue.Places())
	{
		Exported.insert(Place.Key);
	}
	// The field's system images (its window frames, cursor and the like), each through the colours it came with.
	std::vector<FTexturePictureSeed> Seeds;
	AddPictureSeeds(Session->Play->Drawing().Vram().Pictures(), Seeds);
	REQUIRE(Seeds.size() == 7);
	for (const FTexturePictureSeed& Seed : Seeds)
	{
		REQUIRE(Seed.Palette);
		const FTextureRegionTexels Texels = ReadTextureRegionTexels(Session->Play->Drawing().Vram(), Seed.Region);
		CHECK(Exported.contains(TextureRegionKeyWith(Texels, *Seed.Palette)));
	}
}

TEST_CASE("A dungeon map's picture is exported whole, with the key each of its columns is drawn with", "[Content][Game][TextureRegion]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	auto Executable = ReadBootExecutable(*Disc);
	REQUIRE(Executable);
	auto Index = FGameFileIndex::Read(*Executable);
	REQUIRE(Index);
	// Map 239's picture, 320 x 240 8-bit texels, which the field puts at (768, 256) with its colours in row 246 and draws
	// in three columns from the pages at 768, 832 and 896: the middle page begins halfway through the first.
	constexpr int32 PictureFile = 0x7FB + 3;
	const FIndexRecord* Record = Index->Find(4, PictureFile);
	REQUIRE(Record != nullptr);
	const auto Bytes = ReadFileSectors(Disc->Image(), *Record);
	REQUIRE(Bytes);
	auto Tim = ParseTim(*Bytes);
	REQUIRE(Tim);
	REQUIRE(Tim->Clut);
	Tim->Clut->Rect.X = 0;
	Tim->Clut->Rect.Y = 0xF6;
	Tim->Image.Rect.X = 0x300;
	Tim->Image.Rect.Y = 0x100;
	FVram Vram;
	Vram.Upload(*Tim->Clut);
	Vram.Upload(Tim->Image);
	std::set<uint64> Drawn;
	for (const int32 Column : {0, 1, 2})
	{
		std::vector<FTextureRegion> Regions;
		AddPictureRegionsInView(Vram.Pictures(), PackTextureView({0x300 + Column * 0x40, 0x100, ETextureDepth::Indexed8}, {0, 0xF6}), Regions);
		REQUIRE(Regions.size() == 1);
		Drawn.insert(TextureRegionKey(Vram, Regions[0]));
	}
	REQUIRE(Drawn.size() == 1);

	FDiscTextureCatalogue Catalogue(Disc->Image(), *Index);
	std::size_t Item = 0;
	while (Item < Catalogue.TotalItems() && Catalogue.FolderOf(Item) != "files/04/2046")
	{
		++Item;
	}
	REQUIRE(Item < Catalogue.TotalItems());
	CHECK(CatalogueKeys(Catalogue, Item) == Drawn);
	const auto Place = std::find_if(Catalogue.Places().begin(), Catalogue.Places().end(), [&Drawn](const FDiscTexturePlace& Candidate)
	{
		return Drawn.contains(Candidate.Key);
	});
	REQUIRE(Place != Catalogue.Places().end());
	CHECK(Place->Width == 320);
	CHECK(Place->Height == 240);
}
