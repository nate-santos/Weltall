#include "Sound/DiscMusicWaveExport.h"

#include "Foundation/AssetKey.h"
#include "Media/AssetFiles.h"
#include "Media/GameFileIndex.h"
#include "Media/Iso9660.h"
#include "Sound/GameSoundSession.h"
#include "Sound/MusicReplacements.h"
#include "Sound/MusicWaveExport.h"

#include <cstdio>
#include <filesystem>

namespace Weltall
{
	struct FDiscMusicWaveExport::FState
	{
		FState(FIso9660 InDisc, FGameFileIndex InIndex, std::filesystem::path InFolder)
			: Disc(std::move(InDisc)), Index(std::move(InIndex)), Folder(std::move(InFolder)) {}
		FIso9660 Disc;
		FGameFileIndex Index;
		std::filesystem::path Folder;
		std::unique_ptr<FGameSoundSession> Session;
		std::unique_ptr<FMusicWaveExport> Song;
		FDiscMusicWaveExportStatus Progress;
	};

	FDiscMusicWaveExport::FDiscMusicWaveExport(std::unique_ptr<FState> InState) : State(std::move(InState)) {}
	FDiscMusicWaveExport::~FDiscMusicWaveExport() = default;
	const FDiscMusicWaveExportStatus& FDiscMusicWaveExport::Status() const { return State->Progress; }

	TResult<std::unique_ptr<FDiscMusicWaveExport>> FDiscMusicWaveExport::Open(const FIso9660& Disc, const FGameFileIndex& Index,
		std::filesystem::path MusicFolder)
	{
		auto State = std::make_unique<FState>(Disc, Index, std::move(MusicFolder));
		State->Disc.Image().UseDiscAlone();
		auto Started = FGameSoundSession::Start(State->Disc);
		if (!Started) return Started.Error();
		State->Session = std::move(*Started);
		State->Progress.TotalSongs = static_cast<uint32>(State->Session->FieldMusic().SongCount());
		State->Progress.bComplete = State->Progress.TotalSongs == 0;
		return std::unique_ptr<FDiscMusicWaveExport>(new FDiscMusicWaveExport(std::move(State)));
	}

	TResult<bool> FDiscMusicWaveExport::Step(uint32 MaxFrames)
	{
		if (State->Progress.bComplete) return true;
		State->Progress.CurrentSong = static_cast<int32>(State->Progress.CompletedSongs);
		const FIndexRecord* Record = State->Index.Find(28, 0x14 + 2 * State->Progress.CurrentSong);
		if (Record == nullptr || !Record->IsFile()) return MakeError("Music export cannot find the song's source sequence.");
		auto Sequence = ReadFileSectors(State->Disc.Image(), *Record);
		if (!Sequence) return Sequence.Error();
		Sequence->resize(static_cast<std::size_t>(Record->Size));
		char Name[48];
		std::snprintf(Name, sizeof(Name), "song-%02d-%s.wav", State->Progress.CurrentSong, AssetKeyName(MusicSequenceKey(*Sequence)).c_str());
		const std::filesystem::path Relative = std::filesystem::path(FMusicReplacements::OriginalsFolder) / Name;
		if (!State->Song)
		{
			std::error_code Error;
			const bool bExists = std::filesystem::exists(State->Folder / Relative, Error);
			if (Error) return MakeError("Cannot inspect exported music: " + Error.message());
			if (!bExists)
			{
				if (!State->Session)
				{
					auto Started = FGameSoundSession::Start(State->Disc);
					if (!Started) return Started.Error();
					State->Session = std::move(*Started);
				}
				auto Started = FMusicWaveExport::Start(*State->Session, State->Progress.CurrentSong);
				if (!Started) return Started.Error();
				State->Song = std::move(*Started);
			}
			else
			{
				++State->Progress.CompletedSongs;
				State->Progress.bComplete = State->Progress.CompletedSongs == State->Progress.TotalSongs;
				return State->Progress.bComplete;
			}
		}
		const auto Done = State->Song->Step(MaxFrames);
		if (!Done) return Done.Error();
		if (!*Done) return false;
		const auto Bytes = State->Song->Wave().Serialize();
		if (!Bytes) return Bytes.Error();
		const auto Written = PublishAssetFile(State->Folder, Relative, *Bytes);
		if (!Written) return Written.Error();
		State->Song.reset();
		State->Session.reset();
		++State->Progress.CompletedSongs;
		State->Progress.bComplete = State->Progress.CompletedSongs == State->Progress.TotalSongs;
		return State->Progress.bComplete;
	}
}
