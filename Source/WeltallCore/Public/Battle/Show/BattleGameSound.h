#pragma once

#include "Battle/Show/BattleShowSeams.h"
#include "Foundation/Result.h"
#include "Foundation/Types.h"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Weltall
{
	class FGameSoundSystem;
	class FSoundEffectFile;
	struct FResidentSound;
	struct FSoundBank;
	struct FSoundSequence;

	/**
	 * The battle's sound in the game: the show's calls on the session's sound driver. The effect files the battle registers
	 * are kept as the driver reads them, each known by the bytes it was registered from, as the driver knows a file by its
	 * place in memory; the battle's banks take the kernel's gear battle bank's room, which the battle gives back at its end;
	 * the music is the song the field or the world map started. Game thread only.
	 */
	class FBattleGameSound final : public IBattleShowSound
	{
	public:
		/** Music: the song the battle plays, none when nothing plays. Everything must outlive the battle's sound. */
		WELTALLCORE_API FBattleGameSound(FGameSoundSystem& Sound, FResidentSound& Resident, FSoundSequence* Music);
		/** Every effect file still registered leaves the driver before its bytes go. */
		WELTALLCORE_API ~FBattleGameSound() override;
		FBattleGameSound(const FBattleGameSound&) = delete;
		FBattleGameSound& operator=(const FBattleGameSound&) = delete;

		/** The first file the driver could not read or found no room for since the last call; the calls after it went on. */
		WELTALLCORE_API std::optional<FError> TakeError();

		WELTALLCORE_API void PlayEffect(uint32 Sound) override;
		WELTALLCORE_API void PlaySystemEffect(uint32 Sound) override;
		WELTALLCORE_API void StopEffect(uint32 Sound) override;
		WELTALLCORE_API void PlayEffectOn(uint32 Sound, int32 Voice) override;
		WELTALLCORE_API void PlayEffectAt(uint32 Sound, int32 Volume, int32 Pan) override;
		WELTALLCORE_API bool IsEffectPlaying(uint32 Sound) override;
		WELTALLCORE_API void SetEffectVolume(uint32 Sound, int32 Volume) override;
		WELTALLCORE_API void FadeEffectVolume(uint32 Sound, int32 Volume, int32 Steps) override;
		WELTALLCORE_API void SetMusicVolume(int32 Volume, int32 Steps) override;
		WELTALLCORE_API void RegisterEffects(std::span<const uint8> Seds) override;
		WELTALLCORE_API void UnregisterEffects(std::span<const uint8> Seds) override;
		WELTALLCORE_API void StopAllEffects() override;
		WELTALLCORE_API void StopEffects(std::span<const uint8> Seds) override;
		WELTALLCORE_API bool HasEffects(uint16 Id) override;
		WELTALLCORE_API void UnloadGearBattleBank() override;
		WELTALLCORE_API void UnloadBattleBank() override;
		WELTALLCORE_API void LoadBattleBank(std::span<const uint8> Wds) override;
		WELTALLCORE_API void LoadBattleVoiceBank(std::span<const uint8> Wds) override;
		WELTALLCORE_API void LoadGearBattleBank(std::span<const uint8> Wds) override;
		WELTALLCORE_API bool HasBank(uint16 Id) override;
		WELTALLCORE_API void PauseSound() override;
		WELTALLCORE_API void ResumeSound() override;

	private:
		/** An effect file the driver holds, and where the show's bytes it came from lie. */
		struct FRegistered
		{
			const uint8* Source = nullptr;
			std::unique_ptr<FSoundEffectFile> File;
		};

		/** The bank a `wds` file holds loaded where it asks to be, as recorded voices or not; none (and the error kept) when it cannot be. */
		const FSoundBank* Load(std::span<const uint8> Wds, bool bVoices);
		void Fail(FError Error);

		FGameSoundSystem& Sound;
		FResidentSound& Resident;
		FSoundSequence* Music = nullptr;
		std::vector<FRegistered> Registered;
		/** The bank the battle loaded last (the one its unload frees). */
		const FSoundBank* BattleBank = nullptr;
		std::optional<FError> Error;
	};
}
