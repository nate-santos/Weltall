#include "Battle/Show/BattleEventFiles.h"

#include "Formats/Lzss.h"
#include "Foundation/LittleEndian.h"
#include "Media/GameFileIndex.h"

#include <string>
#include <utility>

namespace Weltall
{
	namespace
	{
		/** The event program's directory: the program, its objects' archive and its sound effects. */
		constexpr int32 EventDirectory = 32;
		constexpr int32 EventProgramFile = 1;
		constexpr int32 EventObjectsFile = 3;
		constexpr int32 EventSoundsFile = 4;
		/** Where the program keeps its table of the portraits' files (two bytes a portrait), and the first of those files. */
		constexpr std::size_t PortraitTableOffset = 0x4B5C;
		constexpr int32 PortraitDirectory = 4;
		constexpr int32 FirstPortraitFile = 0x46;
		/** The party's directory, which holds the voices' files after the sprites. */
		constexpr int32 VoiceDirectory = 45;
		/** The unpacker may read a few bytes past a stream's last flag byte. */
		constexpr std::size_t ArchivePadding = 16;

		TResult<std::vector<uint8>> ReadFile(const FCdImage& Image, const FGameFileIndex& Index, int32 Directory, int32 Number, bool bSectors)
		{
			const FIndexRecord* Record = Index.Find(Directory, Number);
			if (Record == nullptr || !Record->IsFile())
			{
				return MakeError("the disc lacks file " + std::to_string(Number) + " of directory " + std::to_string(Directory));
			}
			return bSectors ? ReadFileSectors(Image, *Record) : ReadFileBytes(Image, *Record);
		}
	}

	TResult<FBattleEventFiles> FBattleEventFiles::Load(const FCdImage& Image, const FGameFileIndex& Index,
		const std::array<FBattleProgramTables::FVoiceSet, FBattleProgramTables::VoiceSetCount>& Voices)
	{
		FBattleEventFiles Files;
		Files.Image = &Image;
		Files.Index = &Index;
		Files.SetVoiceFiles(Image, Index);
		Files.VoiceSets = Voices;
		TResult<std::vector<uint8>> Program = ReadFile(Image, Index, EventDirectory, EventProgramFile, false);
		TResult<std::vector<uint8>> Objects = ReadFile(Image, Index, EventDirectory, EventObjectsFile, true);
		TResult<std::vector<uint8>> Sounds = ReadFile(Image, Index, EventDirectory, EventSoundsFile, false);
		if (!Program || !Objects || !Sounds)
		{
			return MakeError("the battle events' files cannot be read");
		}
		if (Program->size() < PortraitTableOffset + 2 * static_cast<std::size_t>(PortraitCount))
		{
			return MakeError("the battle event program is too short to hold its portraits' table");
		}
		for (std::size_t Portrait = 0; Portrait < Files.PortraitFiles.size(); ++Portrait)
		{
			Files.PortraitFiles[Portrait] = {(*Program)[PortraitTableOffset + 2 * Portrait], (*Program)[PortraitTableOffset + 2 * Portrait + 1]};
		}
		if (Objects->size() < 4)
		{
			return MakeError("the battle events' object archive is empty");
		}
		const uint32 Count = LittleEndian::ReadU32(*Objects, 0);
		if (4 + 4 * static_cast<std::size_t>(Count) > Objects->size())
		{
			return MakeError("the battle events' object archive is cut short");
		}
		for (uint32 Entry = 0; Entry < Count; ++Entry)
		{
			const uint32 Offset = LittleEndian::ReadU32(*Objects, 4 + 4 * static_cast<std::size_t>(Entry));
			if (Offset >= Objects->size())
			{
				return MakeError("a battle event's object starts past its archive");
			}
			Files.ObjectOffsets.push_back(Offset);
		}
		Files.ObjectArchive = std::move(*Objects);
		Files.ObjectArchive.resize(Files.ObjectArchive.size() + ArchivePadding, 0);
		Files.SoundEffects = std::move(*Sounds);
		return Files;
	}

	TResult<std::vector<uint8>> FBattleEventFiles::UnpackObject(int32 Entry) const
	{
		if (Entry < 0 || Entry >= ObjectCount())
		{
			return MakeError("a battle event names object " + std::to_string(Entry) + ", past its archive");
		}
		return UnpackLzss(std::span<const uint8>(ObjectArchive).subspan(ObjectOffsets[static_cast<std::size_t>(Entry)]));
	}

	TResult<std::vector<uint8>> FBattleEventFiles::ReadPortrait(int32 Portrait, bool bAtRight) const
	{
		if (Portrait < 0 || Portrait >= PortraitCount)
		{
			return MakeError("a battle event names portrait " + std::to_string(Portrait) + ", past the program's table");
		}
		return ReadFile(*Image, *Index, PortraitDirectory, FirstPortraitFile + PortraitFiles[static_cast<std::size_t>(Portrait)][bAtRight ? 1 : 0], false);
	}

	TResult<std::vector<uint8>> FBattleEventFiles::ReadVoiceSounds(int32 Set) const
	{
		if (!IsVoiceSet(Set))
		{
			return MakeError("a battle event names voice set " + std::to_string(Set) + ", past the program's table");
		}
		return ReadFile(*VoiceImage, *VoiceIndex, VoiceDirectory, VoiceSets[static_cast<std::size_t>(Set)].File, false);
	}

	TResult<std::vector<uint8>> FBattleEventFiles::ReadVoiceBank(int32 Set, int32 Number) const
	{
		if (!IsVoiceSet(Set))
		{
			return MakeError("a battle event names voice set " + std::to_string(Set) + ", past the program's table");
		}
		const int32 First = VoiceSets[static_cast<std::size_t>(Set)].File + 1;
		return ReadFile(*VoiceImage, *VoiceIndex, VoiceDirectory, Set == FBattleProgramTables::SingleBankVoiceSet ? First : First + Number, false);
	}
}
