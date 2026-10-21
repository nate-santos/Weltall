#include "Field/FieldPartySprites.h"

#include "Formats/Lzss.h"
#include "Game/GameState.h"
#include "Media/GameFileIndex.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace Weltall
{
	namespace
	{
		constexpr int32 PartySpriteDirectory = 4;
		constexpr uint8 NoCharacter = FGameState::EmptySlot;
		constexpr uint8 NoGear = 0xFF;
		constexpr uint8 PilotPlaces[FGameState::CharacterCount] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 2, 6};
	}

	std::optional<int32> PilotSpriteFile(int32 Character, int32 Base)
	{
		if (Character < 0 || Character >= FGameState::CharacterCount)
		{
			return std::nullopt;
		}
		return PilotPlaces[static_cast<std::size_t>(Character)] + Base;
	}

	FPartySpriteFiles::FPartySpriteFiles(const FCdImage& InImage, const FGameFileIndex& InIndex)
		: Image(&InImage), Index(&InIndex)
	{
	}

	std::optional<int32> FPartySpriteFiles::FileFor(const FGameState& State, int32 Character, bool bGears)
	{
		if (!bGears)
		{
			return Character;
		}
		const std::optional<uint8> Gear = State.CharacterGear(Character);
		if (!Gear)
		{
			return std::nullopt;
		}
		return *Gear == NoGear ? FirstGearFile : FirstGearFile + *Gear;
	}

	std::optional<FError> FPartySpriteFiles::Unpack(int32 Slot, int32 File)
	{
		const int32 Number = File + DiscFileOffset;
		const std::string Name = "the party's sprite file " + std::to_string(PartySpriteDirectory) + "/" + std::to_string(Number);
		const FIndexRecord* Record = Index->Find(PartySpriteDirectory, Number);
		if (Record == nullptr)
		{
			return MakeError(Name + " is not in the file index");
		}
		auto Sectors = ReadFileSectors(*Image, *Record);
		if (!Sectors)
		{
			return Sectors.Error();
		}
		auto Unpacked = UnpackLzss(*Sectors);
		if (!Unpacked)
		{
			return MakeError(Name + ": " + Unpacked.Error().Message);
		}
		if (Unpacked->size() > BufferSize)
		{
			return MakeError(Name + " unpacks past its buffer");
		}
		FSpriteFile& Buffer = Buffers[static_cast<std::size_t>(Slot)];
		const std::span<const uint8> Before = Buffer.From(0);
		std::vector<uint8> Bytes(BufferSize, 0);
		std::copy(Before.begin(), Before.end(), Bytes.begin());
		std::copy(Unpacked->begin(), Unpacked->end(), Bytes.begin());
		auto Parsed = FSpriteFile::Parse(Bytes);
		if (!Parsed)
		{
			return MakeError(Name + ": " + Parsed.Error().Message);
		}
		Buffer = std::move(*Parsed);
		return std::nullopt;
	}

	void FPartySpriteFiles::Copy(int32 From, int32 To)
	{
		Buffers[static_cast<std::size_t>(To)] = Buffers[static_cast<std::size_t>(From)];
		Files[static_cast<std::size_t>(To)] = Files[static_cast<std::size_t>(From)];
	}

	std::optional<FError> FPartySpriteFiles::ReloadFiles(const std::array<int32, Slots>& HeldFiles)
	{
		Files = HeldFiles;
		for (int32 Slot = 0; Slot < Slots; ++Slot)
		{
			Buffers[static_cast<std::size_t>(Slot)] = FSpriteFile{};
			const int32 File = Files[static_cast<std::size_t>(Slot)];
			if (File == NoFile)
			{
				continue;
			}
			if (auto Error = Unpack(Slot, File))
			{
				return Error;
			}
		}
		return std::nullopt;
	}

	std::optional<FError> FPartySpriteFiles::LoadForMap(bool bGears, const FGameState& State, std::array<uint8, Slots>& FieldParty)
	{
		const EPartySpriteKind Wanted = bGears ? EPartySpriteKind::Gears : EPartySpriteKind::Members;
		if (LoadedKind == Wanted)
		{
			return std::nullopt;
		}
		FieldParty.fill(NoCharacter);
		std::size_t Filled = 0;
		for (const uint8 Member : State.Party())
		{
			if (Member != NoCharacter)
			{
				FieldParty[Filled++] = Member;
			}
		}
		for (int32 Slot = 0; Slot < Slots; ++Slot)
		{
			const uint8 Member = FieldParty[static_cast<std::size_t>(Slot)];
			if (Member == NoCharacter)
			{
				continue;
			}
			const std::optional<int32> File = FileFor(State, Member, bGears);
			if (!File)
			{
				return MakeError("character " + std::to_string(Member) + " has no gear byte in the game state");
			}
			SetFile(Slot, *File);
		}
		LoadedKind = Wanted;
		for (int32 Slot = 0; Slot < Slots; ++Slot)
		{
			if (FieldParty[static_cast<std::size_t>(Slot)] == NoCharacter)
			{
				continue;
			}
			if (auto Error = Unpack(Slot, File(Slot)))
			{
				return Error;
			}
		}
		return std::nullopt;
	}
}
