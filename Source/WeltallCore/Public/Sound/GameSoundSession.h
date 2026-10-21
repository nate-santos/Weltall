#pragma once

#include "Foundation/Result.h"
#include "Media/GameFileIndex.h"
#include "Sound/FieldGameSound.h"
#include "Sound/FieldMusic.h"
#include "Sound/GameSoundSystem.h"
#include "Sound/ResidentSound.h"
#include "Sound/WorldMapMusic.h"

#include <memory>

namespace Weltall
{
	class FIso9660;

	/** How the game's sound reaches the speakers: the title's Sound option. */
	enum class ESoundOutput : uint8
	{
		Stereo,
		/** Stereo with one side of the main output and the other of the reverb inverted. */
		WideStereo,
		Mono,
	};

	/** The sound driver's output mode for a choice: 0 mono, 1 stereo, 2 the wide stereo. */
	WELTALLCORE_API int32 SoundDriverOutputMode(ESoundOutput Output);

	/**
	 * The game's sound for a whole session, set going from a disc as the game's start-up does: the
	 * SPU and the driver (shared with the audio thread that makes the samples), the resident sound,
	 * the field's music, what the field's scripts play through (their songs, effects and bank), and the
	 * world map's music.
	 */
	class FGameSoundSession
	{
	public:
		WELTALLCORE_API static TResult<std::unique_ptr<FGameSoundSession>> Start(const FIso9660& Disc);

		WELTALLCORE_API ~FGameSoundSession();
		FGameSoundSession(const FGameSoundSession&) = delete;
		FGameSoundSession& operator=(const FGameSoundSession&) = delete;

		const std::shared_ptr<FGameSoundSystem>& Sound() const { return GameSound; }
		FResidentSound& Resident() const { return *ResidentSound; }
		FFieldMusic& FieldMusic() const { return *Music; }
		FFieldGameSongs& FieldSongs() const { return *Songs; }
		FFieldGameSoundEffects& FieldSoundEffects() const { return *Effects; }
		FWorldMapMusic& WorldMapMusic() const { return *WorldMap; }

		/**
		 * The player's choices for the sound, at once: the output (set only when it changes, as the
		 * title's option sets it), the volumes, the driver's and the field music's fixes.
		 */
		WELTALLCORE_API void Apply(ESoundOutput Output, const FSoundVolumes& Volumes, const FSoundDriverOptions& DriverOptions, const FFieldMusicOptions& MusicOptions);

	private:
		FGameSoundSession() = default;

		std::shared_ptr<FGameSoundSystem> GameSound;
		std::unique_ptr<FGameFileIndex> Index;
		std::unique_ptr<FResidentSound> ResidentSound;
		std::unique_ptr<FFieldMusic> Music;
		std::unique_ptr<FFieldGameSongs> Songs;
		std::unique_ptr<FFieldGameSoundEffects> Effects;
		/** Made after the field's music, which it calls on. */
		std::unique_ptr<FWorldMapMusic> WorldMap;
	};
}
