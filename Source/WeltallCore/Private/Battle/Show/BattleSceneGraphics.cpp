#include "Battle/Show/BattleSceneGraphics.h"

#include "Foundation/LittleEndian.h"
#include "Media/GameFileIndex.h"

#include <string>
#include <utility>

namespace Weltall
{
	namespace
	{
		using namespace LittleEndian;

		constexpr uint32 MaxImages = 0x40;
	}

	TResult<FBattleSceneGraphics> FBattleSceneGraphics::Parse(std::vector<uint8> File)
	{
		if (File.size() < EntriesOffset)
		{
			return MakeError("a scene's graphics file is shorter than its header");
		}
		const std::size_t Count = File[0];
		const std::size_t DataStart = EntriesOffset + Count * EntrySize;
		const uint32 End = ReadU32(File, 4);
		if (DataStart > File.size() || End < DataStart || End > File.size())
		{
			return MakeError("a scene's graphics entries or kept data run past the file");
		}
		FBattleSceneGraphics Graphics;
		Graphics.End = End;
		for (std::size_t Entry = 0; Entry < Count; ++Entry)
		{
			const std::size_t At = EntriesOffset + Entry * EntrySize;
			FBattleSceneEntry Read;
			Read.Data = ReadU32(File, At);
			Read.Images = ReadU32(File, At + 4);
			Read.Kind = File[At + 8];
			Read.Byte9 = File[At + 9];
			Read.Byte10 = File[At + 10];
			Read.PaletteSet = File[At + 11];
			if ((Read.Data > 7 && (Read.Data < DataStart || Read.Data >= End)) || (Read.HasOwnImages() && Read.Images >= File.size()))
			{
				return MakeError("a scene's graphics entry " + std::to_string(Entry) + " points past the file");
			}
			Graphics.SceneEntries.push_back(Read);
		}
		Graphics.File = std::move(File);
		return Graphics;
	}

	std::vector<int32> FBattleSceneGraphics::Scenes(const FGameFileIndex& Index)
	{
		std::vector<int32> Held;
		// Past the directory's last file, a number names the files of the directories after it.
		for (int32 Scene = 0; const FIndexRecord* Record = Index.Find(SceneDirectory, 2 * Scene + 3); ++Scene)
		{
			const std::optional<FGameFileIndex::FFileName> Name = Index.NameOf(static_cast<std::size_t>(Record - Index.Records().data()));
			if (!Name || Name->Directory != SceneDirectory)
			{
				break;
			}
			if (Record->IsFile())
			{
				Held.push_back(Scene);
			}
		}
		return Held;
	}

	TResult<FBattleSceneGraphics> FBattleSceneGraphics::Load(const FCdImage& Image, const FGameFileIndex& Index, int32 Scene)
	{
		const FIndexRecord* Record = Index.Find(SceneDirectory, 2 * Scene + 3);
		if (Scene < 0 || Record == nullptr || !Record->IsFile())
		{
			return MakeError("the disc lacks scene " + std::to_string(Scene) + "'s graphics");
		}
		TResult<std::vector<uint8>> Bytes = ReadFileBytes(Image, *Record);
		if (!Bytes)
		{
			return Bytes.Error();
		}
		return Parse(std::move(*Bytes));
	}

	TResult<std::vector<FBattleImageSetImage>> ReadBattleImageSet(std::span<const uint8> File, uint32 Offset)
	{
		if (std::size_t{Offset} + 4 > File.size())
		{
			return MakeError("an image set lies past its file");
		}
		const uint32 Count = ReadU32(File, Offset);
		if (Count > MaxImages || std::size_t{Offset} + 4 + 4 * std::size_t{Count} > File.size())
		{
			return MakeError("an image set's table runs past its file");
		}
		std::vector<FBattleImageSetImage> Images;
		for (uint32 Image = 0; Image < Count; ++Image)
		{
			const std::size_t At = std::size_t{Offset} + ReadU32(File, Offset + 4 + 4 * std::size_t{Image});
			if (At + 4 > File.size())
			{
				return MakeError("an image set's image lies past its file");
			}
			const FBattleImageSetImage Read{ReadU16(File, At), ReadU16(File, At + 2), static_cast<uint32>(At + 4)};
			if (At + 4 + 2 * std::size_t{Read.Width} * Read.Height > File.size())
			{
				return MakeError("an image set's image runs past its file");
			}
			Images.push_back(Read);
		}
		return Images;
	}
}
