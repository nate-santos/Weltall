#pragma once

#include "Foundation/Result.h"
#include "Foundation/Types.h"
#include "Graphics/SpriteFile.h"

#include <array>
#include <optional>

namespace Weltall
{
	class FCdImage;
	class FGameFileIndex;
	class FGameState;

	/**
	 * The map sprite file a character's pilot is drawn with while the party is in gears: Base (the map's GearMode) plus
	 * the character's place in the map's set of pilots, which the second records of Citan and Emeralda share with the
	 * first. None for a number past the characters.
	 */
	WELTALLCORE_API std::optional<int32> PilotSpriteFile(int32 Character, int32 Base);

	/** Which files a map last loaded into the party's sprite buffers: none yet, the members' own or their gears'. */
	enum class EPartySpriteKind : uint8
	{
		None = 0,
		Members = 1,
		Gears = 2,
	};

	/**
	 * The party's field sprites as the kernel keeps them from map to map: a buffer of 0x14000 bytes
	 * per party slot holding an unpacked sprite file (the slot's member's own, or its gear's), which
	 * file each slot holds, and which kind of file a map last loaded. A sprite made from a slot reads
	 * the slot's buffer as it is when it reads it: a file unpacked or copied over the buffer later is
	 * read through the tables the sprite took from the one before, and the bytes past a shorter file
	 * are the ones it left. Files are read from the disc's directory 4 at once.
	 */
	class FPartySpriteFiles
	{
	public:
		static constexpr int32 Slots = 3;
		static constexpr uint32 BufferSize = 0x14000;
		/** The file a slot holds when it holds none. */
		static constexpr int32 NoFile = 0xFF;
		/** A slot's file is a character's number, or this plus its gear's (plus nothing without a gear). */
		static constexpr int32 FirstGearFile = 0x10;
		/** A file's number in directory 4 is this past the file a slot names. */
		static constexpr int32 DiscFileOffset = 5;

		/** The buffers as the field finds them on entering: each holding nothing, no kind loaded. */
		WELTALLCORE_API FPartySpriteFiles(const FCdImage& Image, const FGameFileIndex& Index);

		/** A slot's buffer, as the sprites made from the slot read it; empty until a file was unpacked into it. */
		const FSpriteFile& Buffer(int32 Slot) const { return Buffers[static_cast<std::size_t>(Slot)]; }
		/** The file a slot holds, NoFile for none. */
		int32 File(int32 Slot) const { return Files[static_cast<std::size_t>(Slot)]; }
		EPartySpriteKind Kind() const { return LoadedKind; }

		/**
		 * The file a character's sprite comes from: its own number, or in gears its gear's file (the
		 * first gear file when it has none); nothing when the game state holds no gear byte for it.
		 */
		WELTALLCORE_API static std::optional<int32> FileFor(const FGameState& State, int32 Character, bool bGears);

		void SetFile(int32 Slot, int32 File) { Files[static_cast<std::size_t>(Slot)] = File; }
		/** Unpacks the disc's file for File over the slot's buffer; the buffer's bytes past it stay. */
		WELTALLCORE_API std::optional<FError> Unpack(int32 Slot, int32 File);
		/** Copies one slot's whole buffer, and the file it holds, over another's. */
		WELTALLCORE_API void Copy(int32 From, int32 To);

		/**
		 * The party's sprites a map needs before its scripts run, the members' or their gears': unless
		 * that kind is the one loaded, the game state's party, gaps closed, becomes the field's party,
		 * and each member's file is unpacked into its slot. Empty slots keep their buffer and file.
		 */
		WELTALLCORE_API std::optional<FError> LoadForMap(bool bGears, const FGameState& State, std::array<uint8, Slots>& FieldParty);

		/** The field leaves for another program: the next map it loads reads the party's files again. */
		void ForgetLoadedKind() { LoadedKind = EPartySpriteKind::None; }

		/**
		 * The field starts again after a battle with the files the kernel's slots held as it left: into
		 * new buffers, each slot's file read again (the kind stays none, as the field left it, and the
		 * field's party is not made again).
		 */
		WELTALLCORE_API std::optional<FError> ReloadFiles(const std::array<int32, Slots>& HeldFiles);

	private:
		const FCdImage* Image;
		const FGameFileIndex* Index;
		std::array<FSpriteFile, Slots> Buffers;
		std::array<int32, Slots> Files{NoFile, NoFile, NoFile};
		EPartySpriteKind LoadedKind = EPartySpriteKind::None;
	};
}
