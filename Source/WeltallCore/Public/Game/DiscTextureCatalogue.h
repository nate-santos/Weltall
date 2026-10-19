#pragma once

#include "Foundation/Result.h"
#include "Foundation/Types.h"
#include "Graphics/Texture.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace Weltall
{
	class FCdImage;
	class FGameFileIndex;
	class FVram;
	class FBattleSpriteFiles;
	struct FBattleObjectTables;
	struct FTexturePictureSeed;
	struct FTextureRegion;

	/**
	 * An original picture as the texture export saves it: on its own in the folder of the item it was first found in, or
	 * on the item's sheet of that name, which holds every cell a streamed sprite file shows under one palette set.
	 */
	struct FDiscTexture
	{
		std::size_t Item = 0;
		uint64 Key = 0;
		FImageRgba8 Image;
		std::string Sheet;
	};

	/** An item a picture appears in, for the export's index of where each one is drawn. */
	struct FDiscTexturePlace
	{
		uint64 Key = 0;
		uint32 Item = 0;
		uint16 Width = 0;
		uint16 Height = 0;
	};

	/**
	 * Every texture picture the disc's data defines, as play identifies it (Weltall::TextureRegionKey): the cells of the
	 * sprite files the field streams (the party's and their gears', the extra animation sets), then each field map's
	 * scenery and panoramas, the sheets of its resident sprites under every palette set and a streamed map sprite's cells, then the
	 * TIM pictures every other file holds, as it is, unpacked or in a container's sections (the world map's, the
	 * field's system images and screens, the menus', Battling's), each whole through its own colours, then Battle's
	 * stages, each scene's enemies (its models, its sprites' cells), every gear with each of its variants, the special
	 * models, the party's battle sprites and attacks, the common effects and every effect's sprites, the battle HUD's and the
	 * result screen's pictures (each character's portrait among them), then the gear models
	 * the field and the gear shop load. A
	 * streamed file's cells come with the name of the sheet of its palette set they are saved on. Each item is loaded
	 * through the game's own readers into a VRAM of its own, so a picture's key is the one its drawing gives it.
	 */
	class FDiscTextureCatalogue
	{
	public:
		WELTALLCORE_API FDiscTextureCatalogue(const FCdImage& Image, const FGameFileIndex& Index);
		WELTALLCORE_API ~FDiscTextureCatalogue();

		/**
		 * The next item's pictures, leaving out those with no texel to show and, unless they belong on a sheet, those an
		 * earlier item had. An item that cannot be read gives its error, and the next Step goes on with the item after it.
		 */
		WELTALLCORE_API TResult<std::vector<FDiscTexture>> Step();

		bool IsComplete() const { return Next >= Items.size(); }
		std::size_t CompletedItems() const { return Next; }
		std::size_t TotalItems() const { return Items.size(); }
		/**
		 * An item's folder: "field/0002" for map 2, "sprites/04-0005" for sprite file 5 of directory 4, "files/36/0002" for
		 * file 2 of directory 36, "battle/stages/007", "battle/enemies/012" (scene 12), "battle/gears/03-1" (gear 3, variant
		 * 1), "battle/special/2", "battle/party/03" (character 3's battle sprites), "battle/effects/common",
		 * "battle/effects/042" (effect 42), "battle/hud", "battle/result", "gears/07" (the field's gear model 7).
		 */
		WELTALLCORE_API std::string FolderOf(std::size_t Item) const;
		/** Every item each picture was found in so far, in the order found. */
		const std::vector<FDiscTexturePlace>& Places() const { return Found; }

	private:
		enum class EItem : uint8
		{
			SpriteFile,
			Map,
			/** A file's TIMs. */
			Pictures,
			/** Battle's 3D models of a source (Directory holds the EBattleModelSource, File a gear's variant). */
			BattleModels,
			/** A character's battle sprites and attacks. */
			BattleParty,
			/** An effect's sprites (Number), or the common effects' (Number -1). */
			BattleEffect,
			/** The battle HUD's pictures (Number 0), or the result screen's (Number 1). */
			BattleHud,
			/** A gear model of the field's (FGearFileNumbers). */
			FieldGear,
		};

		struct FItem
		{
			EItem Kind = EItem::SpriteFile;
			/** A map's number, or a record's for a file's pictures. */
			int32 Number = 0;
			int32 Directory = 0;
			int32 File = 0;
		};

		std::optional<FError> AddMap(int32 MapNumber, std::vector<FDiscTexture>& Pictures);
		std::optional<FError> AddSpriteFile(int32 Directory, int32 File, std::vector<FDiscTexture>& Pictures);
		std::optional<FError> AddFilePictures(std::size_t Record, std::vector<FDiscTexture>& Pictures);
		std::optional<FError> AddBattleModels(const FItem& Item, std::vector<FDiscTexture>& Pictures);
		std::optional<FError> AddBattleParty(int32 Character, std::vector<FDiscTexture>& Pictures);
		std::optional<FError> AddBattleEffect(int32 Effect, std::vector<FDiscTexture>& Pictures);
		std::optional<FError> AddBattleHud(bool bResult, std::vector<FDiscTexture>& Pictures);
		std::optional<FError> AddFieldGear(int32 GearModel, std::vector<FDiscTexture>& Pictures);
		void AddSeeds(const FVram& Vram, std::span<const FTexturePictureSeed> Seeds, std::vector<FDiscTexture>& Pictures);
		void AddPictures(std::span<const uint8> Bytes, std::vector<FDiscTexture>& Pictures);
		/** A region through its view's CLUT, or through Palette when one is given (none for a direct picture). */
		void Add(const FVram& Vram, const FTextureRegion& Region, std::vector<FDiscTexture>& Pictures, const std::string& Sheet = {},
			const std::optional<std::span<const uint16>>& Palette = std::nullopt);

		const FCdImage& Image;
		const FGameFileIndex& Index;
		std::vector<FItem> Items;
		std::size_t Next = 0;
		std::unordered_set<uint64> Seen;
		std::unordered_set<uint64> PlacedInItem;
		std::unordered_set<uint64> SeenOnSheets;
		std::vector<FDiscTexturePlace> Found;
		std::unique_ptr<FVram> PictureVram;
		std::unique_ptr<FBattleObjectTables> BattleTables;
		std::unique_ptr<FBattleSpriteFiles> BattleSprites;
	};
}
