#pragma once

#include "Formats/Wave.h"
#include "Foundation/Result.h"
#include "Media/GameFileIndex.h"
#include "Media/CdImage.h"
#include "Sound/MusicReplacements.h"
#include "Sound/SoundFiles.h"

#include <string>
#include <vector>
#include <type_traits>

namespace Weltall
{
	/** A sound file of the game, found by the directory and number the game names it by. */
	template <typename TFile>
	TResult<TFile> ReadSoundFile(const FCdImage& Image, const FGameFileIndex& Index, int32 Directory, int32 Number)
	{
		const FIndexRecord* Record = Index.Find(Directory, Number);
		if (Record == nullptr || !Record->IsFile())
		{
			return MakeError("the disc lacks file " + std::to_string(Number) + " of directory " + std::to_string(Directory));
		}
		TResult<std::vector<uint8>> Bytes = ReadFileBytes(Image, *Record);
		if (!Bytes)
		{
			return Bytes.Error();
		}
		TResult<TFile> Parsed = TFile::Parse(std::move(*Bytes));
		if constexpr (std::is_same_v<TFile, FSoundSequenceFile>)
		{
			if (Parsed)
			{
				TResult<std::optional<std::vector<uint8>>> Override = Image.ReadMusicReplacement(MusicSequenceKey(Parsed->Bytes()));
				if (!Override) return Override.Error();
				if (*Override)
				{
					TResult<FWave> Wave = FWave::Parse(**Override);
					if (!Wave) return Wave.Error();
					Parsed->Replacement = std::make_shared<const FWave>(std::move(*Wave));
				}
			}
		}
		return Parsed;
	}
}
