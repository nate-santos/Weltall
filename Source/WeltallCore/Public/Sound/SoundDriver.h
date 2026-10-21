#pragma once

#include "Foundation/Types.h"

#include <memory>
#include <span>

namespace Weltall
{
	class ISpuBus;
	class FSoundBankFile;
	class FSoundEffectFile;
	class FSoundSequenceFile;
	struct FSoundDriverTables;

	/** A bank the driver has loaded. */
	struct FSoundBank;
	/** A sequence the driver has loaded: a song, with its tracks' state. */
	struct FSoundSequence;

	/** What a voice plays, as the player's volumes tell them apart: a song, a sound effect, or a recorded voice. */
	enum class ESoundKind : uint8
	{
		Music,
		Effect,
		Voice,
	};

	/** Where the driver departs from the original: fixes for its broken effects, each one a setting. */
	struct FSoundDriverOptions
	{
		/** Pan slides end on the pan they slide to; the original ends them on the slide's difference. */
		bool bPanSlidesEndOnTarget = true;
		/** Notes above the SPU's range play at its highest pitch; the original wraps them to low pitches. */
		bool bHighNotesStayHigh = true;
	};

	/**
	 * The game's sound driver: music sequences and sound effects played from instrument banks on the
	 * SPU's 24 voices, with the game's own envelopes, LFOs, slides, voice stealing and reverb.
	 *
	 * It runs on the timer interrupt (Interrupt(), about 240 times a second), and writes the SPU's
	 * registers, and sound RAM through DMA, exactly as the original does. Calls take effect at the
	 * next interrupt unless they write the SPU themselves. Counts of "interrupts" time fades; effect
	 * volume fades count sequence ticks. Volumes are 0-127 unless stated, pans 0 (left) to 127.
	 *
	 * Files are the caller's: a bank's may go once it is loaded, but a sequence's must outlive the
	 * sequence and an effect file must stay until it is unregistered. Nothing allocates after
	 * construction; when the fixed room for banks, effect files or sequences is full, loading fails.
	 */
	class FSoundDriver
	{
	public:
		static constexpr int32 VoiceCount = 24;
		static constexpr int32 MaxBanks = 16;
		static constexpr int32 MaxEffectFiles = 16;
		static constexpr int32 MaxSequences = 4;
		/** Asks PlayingEffects for every effect. */
		static constexpr uint32 AnyEffect = 0xFFFFFFFF;

		/** Starts the sound library and the driver as the game's start-up does. */
		WELTALLCORE_API FSoundDriver(ISpuBus& Spu, const FSoundDriverTables& Tables, const FSoundDriverOptions& Options = {});
		WELTALLCORE_API ~FSoundDriver();
		FSoundDriver(const FSoundDriver&) = delete;
		FSoundDriver& operator=(const FSoundDriver&) = delete;

		/** One tick of the driver's timer. */
		WELTALLCORE_API void Interrupt();
		/** Mixes replacement music after the SPU, under the same lock and interrupt timing, at Gain (8000h as made). */
		WELTALLCORE_API void MixReplacementMusic(std::span<int16> Stereo, int32 Gain = 0x8000);

		/**
		 * Reserves sound RAM for a bank and uploads its samples: with SpuAddress -1 anywhere, otherwise
		 * where the bank asks to be (with 0, anywhere if it does not say; like the original, any other
		 * value only asks for the bank's own address). Null when there is no room.
		 */
		WELTALLCORE_API const FSoundBank* LoadBank(const FSoundBankFile& File, int32 SpuAddress = 0);
		/** As LoadBank, for a bank of recorded voices: an effect's notes from it are voices to the player's volumes. */
		WELTALLCORE_API const FSoundBank* LoadVoiceBank(const FSoundBankFile& File, int32 SpuAddress = 0);
		/** As LoadBank, but refused when a bank with the same id is loaded (how the field loads its music). */
		WELTALLCORE_API const FSoundBank* LoadNewBank(const FSoundBankFile& File, int32 SpuAddress = 0);
		WELTALLCORE_API void UnloadBank(const FSoundBank& Bank);
		WELTALLCORE_API const FSoundBank* FindBank(int32 Id) const;
		/** Where the bank's samples lie in sound RAM. */
		WELTALLCORE_API static uint32 BankAddress(const FSoundBank& Bank);

		/** Makes a file's effects playable; false when a file with its id is registered or there is no room. */
		WELTALLCORE_API bool RegisterEffects(const FSoundEffectFile& File);
		/** Stops the file's effects and forgets it. */
		WELTALLCORE_API void UnregisterEffects(const FSoundEffectFile& File);
		WELTALLCORE_API const FSoundEffectFile* FindEffects(uint16 FileId) const;

		/** Loads a sequence without playing it; null when there is no room. */
		WELTALLCORE_API FSoundSequence* LoadSequence(const FSoundSequenceFile& File);
		/** Loads and plays, fading in to Volume over Interrupts (0: at once). */
		WELTALLCORE_API FSoundSequence* StartSequence(const FSoundSequenceFile& File, int32 Volume, int32 Interrupts);
		/** Plays from the start, fading in to Volume over Interrupts. */
		WELTALLCORE_API void PlaySequence(FSoundSequence& Sequence, int32 Volume, int32 Interrupts);
		WELTALLCORE_API void StopSequence(FSoundSequence& Sequence);
		/** Stops a sequence and forgets it; its file may go. */
		WELTALLCORE_API void FreeSequence(FSoundSequence& Sequence);
		WELTALLCORE_API void StopAllSequences();
		/** Continues a stopped sequence where it was, its banks found again, fading in to Volume. */
		WELTALLCORE_API void ResumeSequence(FSoundSequence& Sequence, int32 Volume, int32 Interrupts);
		/** Fades to Volume; a sequence faded to 0 pauses, and fading it back up continues it. */
		WELTALLCORE_API void SetSequenceVolume(FSoundSequence& Sequence, int32 Volume, int32 Interrupts);
		/** Transposes every track by Semitones. */
		WELTALLCORE_API void SetSequencePitch(FSoundSequence& Sequence, int32 Semitones, int32 Interrupts);
		/** Moves every track's pan by Offset (-128 to 127). */
		WELTALLCORE_API void SetSequencePan(FSoundSequence& Sequence, int32 Offset, int32 Interrupts);
		/** Speed as a multiple of 100h (0: normal speed). */
		WELTALLCORE_API void SetTempoScale(FSoundSequence& Sequence, int32 Scale, int32 Interrupts);
		/** Tracks with their bit set go silent; cleared bits bring them back. */
		WELTALLCORE_API void MuteTracks(FSoundSequence& Sequence, uint32 Mask);
		/** Plays on without sound until Measure, then back at normal speed (pair it with a high tempo scale). */
		WELTALLCORE_API void SeekSequence(FSoundSequence& Sequence, uint16 Measure);

		/** An effect on the first free pair of the automatic channels (12, 10, 8), else the pair a normal effect has held longest. */
		WELTALLCORE_API void PlayEffect(uint32 Id);
		WELTALLCORE_API void PlayEffectVolumePan(uint32 Id, int32 Volume, int32 Pan);
		/** An effect on a fixed channel (0, 2, ... 14). */
		WELTALLCORE_API void PlayEffectOn(uint32 Id, int32 Channel);
		WELTALLCORE_API void PlayEffectOnVolumePan(uint32 Id, int32 Channel, int32 Volume, int32 Pan);
		/** Menus' and battles' system sounds, on channels no other effect takes. */
		WELTALLCORE_API void PlaySystemEffect(uint32 Id);
		/** A lasting effect on the automatic effects' first channels, protected from them (the world map's vehicles). */
		WELTALLCORE_API void PlayProtectedEffect(uint32 Id);
		WELTALLCORE_API void StopEffect(uint32 Id);
		WELTALLCORE_API void StopEffectOn(int32 Channel);
		WELTALLCORE_API void StopEffectsOfFile(const FSoundEffectFile& File);
		WELTALLCORE_API void StopAllEffects();
		WELTALLCORE_API void SetEffectVolume(uint32 Id, int32 Volume);
		WELTALLCORE_API void SetEffectVolumeOn(int32 Channel, int32 Volume);
		/** Slides an effect's volume over Ticks of its own. */
		WELTALLCORE_API void FadeEffectVolume(uint32 Id, int32 Volume, int32 Ticks);
		WELTALLCORE_API void FadeEffectVolumeOn(int32 Channel, int32 Volume, int32 Ticks);
		WELTALLCORE_API void SetEffectPanOn(int32 Channel, int32 Pan);
		/** Bit n set: effect track n (channel n) plays effect Id (AnyEffect: any effect). */
		WELTALLCORE_API uint32 PlayingEffects(uint32 Id) const;

		/** The main volume, 0-3FFFh, at once or over Steps of two interrupts. */
		WELTALLCORE_API void SetMasterVolume(int32 Volume, int32 Steps);
		/** The CD input's volume (movie sound), 0-7FFFh, at once or over Steps of two interrupts. */
		WELTALLCORE_API void SetCdVolume(int32 Volume, int32 Steps);
		WELTALLCORE_API void SetCdAttributes(bool bReverb, bool bMix);
		/** 0 mono, 1 stereo, 2 and 3 stereo with one side of the main or reverb output inverted. */
		WELTALLCORE_API void SetOutputMode(int32 Mode);
		/** 0 mono, 1 stereo, 2 either inverted mode. */
		WELTALLCORE_API int32 OutputMode() const;
		/** The fixes the driver plays with from now on. */
		WELTALLCORE_API void SetOptions(const FSoundDriverOptions& Options);
		/**
		 * Reverb type 0-9 (-1: keep the current one), its depth (as the output volume), delay and
		 * feedback (0-127, for the echo and delay types). A new type clears its work area in sound RAM.
		 */
		WELTALLCORE_API void SetReverb(int32 Type, int32 Depth, int32 Delay, int32 Feedback);

		/** What SPU voice Voice (0-23) plays: a song's track, or an effect's from a bank of voices or not, as of its last note. */
		WELTALLCORE_API ESoundKind VoiceKind(int32 Voice) const;

		/** Freezes every voice where it is; the interrupt does nothing until Resume. */
		WELTALLCORE_API void Pause();
		WELTALLCORE_API void Resume();

	private:
		struct FState;
		std::unique_ptr<FState> State;
	};
}
