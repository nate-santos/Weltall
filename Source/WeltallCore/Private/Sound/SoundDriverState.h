#pragma once

#include "Sound/SoundDriver.h"
#include "Sound/SoundDriverTables.h"
#include "Sound/SoundFiles.h"
#include "Sound/SoundRam.h"
#include "Sound/SpuLibrary.h"

#include <array>
#include <span>

namespace Weltall
{
	/** A place in a file's commands, or nowhere. Reads within a command are safe: the files were checked. */
	struct FSoundCursor
	{
		const uint8* Data = nullptr;
		uint32 Size = 0;
		uint32 Offset = 0;

		static FSoundCursor In(std::span<const uint8> Bytes, uint32 At) { return {Bytes.data(), static_cast<uint32>(Bytes.size()), At}; }

		bool IsSet() const { return Data != nullptr; }
		bool Points(std::span<const uint8> Bytes) const { return Data == Bytes.data(); }
		uint8 Byte(uint32 Delta = 0) const { return Data[Offset + Delta]; }
		/** Whether a byte Delta on is still in the file (reading ahead can run past a stream). */
		bool Holds(uint32 Delta) const { return Offset + Delta < Size; }
		FSoundCursor At(uint32 Delta) const { return {Data, Size, Offset + Delta}; }
	};

	/** A value moving to a target: Count steps left, the last one landing exactly on Target << 16. */
	struct FSoundFade
	{
		int32 Value = 0;
		int32 Step = 0;
		int16 Count = 0;
		int16 Target = 0;

		void Run();
	};

	namespace TrackFlag
	{
		inline constexpr uint16 Active = 0x0001;
		inline constexpr uint16 FromEffectFile = 0x0002;
		inline constexpr uint16 IgnoreVelocity = 0x0008;
		inline constexpr uint16 Percussion = 0x0010;
		inline constexpr uint16 Muted = 0x0020;
		inline constexpr uint16 NotePlayed = 0x0080;
		inline constexpr uint16 StepDone = 0x0100;
		inline constexpr uint16 Sustained = 0x0200;
		inline constexpr uint16 Released = 0x0400;
		inline constexpr uint16 Legato = 0x0800;
		inline constexpr uint16 NoteAhead = 0x1000;
		inline constexpr uint16 InstrumentChanged = 0x8000;
	}

	namespace TrackRequest
	{
		inline constexpr uint16 KeyOn = 0x0001;
		inline constexpr uint16 KeyOff = 0x0002;
		inline constexpr uint16 Volume = 0x0100;
		inline constexpr uint16 Pitch = 0x0200;
	}

	namespace TrackSlide
	{
		inline constexpr uint16 Pitch = 0x0001;
		inline constexpr uint16 HoldPitch = 0x0002;
		inline constexpr uint16 Portamento = 0x0004;
		inline constexpr uint16 Volume = 0x0008;
		inline constexpr uint16 Pan = 0x0010;
		inline constexpr uint16 Expression = 0x0020;
		inline constexpr uint16 Sweep = 0x0100;
	}

	/** The voice registers a track asks the next flush to write. */
	namespace VoiceRewrite
	{
		inline constexpr uint16 Volumes = 0x0001;
		inline constexpr uint16 Pitch = 0x0004;
		inline constexpr uint16 Addresses = 0x0008;
		inline constexpr uint16 Attack = 0x0010;
		inline constexpr uint16 Decay = 0x0020;
		inline constexpr uint16 SustainRate = 0x0040;
		inline constexpr uint16 Release = 0x0080;
		inline constexpr uint16 SustainLevel = 0x0100;
		inline constexpr uint16 Envelope = 0x01F0;
		inline constexpr uint16 PitchModulation = 0x1000;
		inline constexpr uint16 Noise = 0x2000;
		inline constexpr uint16 Reverb = 0x4000;
		inline constexpr uint16 Switches = 0x7000;
		inline constexpr uint16 All = 0xFFFF;
	}

	namespace VoiceSwitch
	{
		inline constexpr uint16 PitchModulation = 0x0010;
		inline constexpr uint16 Noise = 0x0020;
		inline constexpr uint16 Reverb = 0x0040;
	}

	namespace SequenceFlag
	{
		inline constexpr uint16 HasFile = 0x0001;
		inline constexpr uint16 ReverbRestricted = 0x0006;
		inline constexpr uint16 Seeking = 0x0020;
		inline constexpr uint16 WaitsForVolume = 0x0100;
		inline constexpr uint16 Playing = 0x8000;
	}

	namespace DriverFlag
	{
		inline constexpr uint16 ReverbOnFileTracks = 0x2000;
		inline constexpr uint16 SequencesSetReverb = 0x1000;
		inline constexpr uint16 EffectsEnabled = 0x0800;
		inline constexpr uint16 OutputMode = 0x0700;
		inline constexpr uint16 Stereo = 0x0100;
		/** The inverted modes: the main output's right and the reverb's left side, or the other way round. */
		inline constexpr uint16 InvertMainRight = 0x0200;
		inline constexpr uint16 InvertMainLeft = 0x0400;
		inline constexpr uint16 Inverted = 0x0600;
		inline constexpr uint16 AllowDuplicateEffects = 0x0080;
		inline constexpr uint16 Paused = 0x0040;
		inline constexpr uint16 ClearingReverb = 0x0020;
		/** What the game's start-up sets: effects and reverb on, sequences choosing their reverb. */
		inline constexpr uint16 Started = 0xB801;
	}

	struct FSoundLfo
	{
		static constexpr uint16 On = 0x0001;
		static constexpr uint16 RestartsAtNotes = 0x0002;
		static constexpr uint16 FirstHalfDone = 0x0004;
		static constexpr uint16 GoingDown = 0x0008;

		/** The waveform it runs: 0-7, or 8 for off. */
		uint8 Waveform = 0;
		int32 Value = 0;
		int32 Step = 0;
		int32 Size = 0;
		uint16 Countdown = 0;
		uint16 Period = 0;
		uint16 DelayLeft = 0;
		uint16 Delay = 0;
		/** Fade-in scale, 400h for full depth. */
		int16 Fade = 0;
		int16 FadeStep = 0;
		/** 0 pitch, 1 volume, 2 pan. */
		uint8 Target = 0;
		/** The waveform number the commands set (0-15). */
		uint8 Wave = 0;
		uint16 Flags = 0;
	};

	struct FSoundRepeat
	{
		/** Passes left, counted down to 0 (wrapping when the last one ends). */
		uint8 Count = 0;
		uint8 Octave = 0;
		uint8 EndOctave = 0;
		FSoundCursor Start;
		FSoundCursor Exit;
	};

	/** A track of a sequence, or a channel of the effect player. */
	struct FSoundTrack
	{
		static constexpr int32 RepeatDepth = 4;
		static constexpr int32 LfoCount = 4;
		static constexpr uint8 NoVoice = 0xFF;

		uint16 Flags = 0;
		uint16 Requests = 0;
		uint16 Slides = 0;
		uint8 Number = 0;
		/** Music 10h; effects 20h, or 60h and 80h for those no other effect may take over. */
		uint8 EffectClass = 0;
		uint32 EffectId = 0;
		uint32 EffectTime = 0;
		FSoundCursor Current;
		FSoundCursor LoopPoint;
		uint16 Loops = 0;
		uint64 FirstLoopFrame = 0;
		uint64 FirstLoopEndFrame = 0;
		uint8 LoopOctave = 0;
		uint8 BankId = 0;
		uint8 Instrument = 0;
		uint8 Voice = NoVoice;
		uint8 InstrumentRelease = 0;
		const FSoundBank* Bank = nullptr;
		/** A song's track plays music; an effect's channel plays what its instrument's bank holds. */
		ESoundKind Kind = ESoundKind::Effect;

		// What the SPU flush writes to the track's voice.
		uint16 Switches = 0;
		int16 Priority = 0;
		uint16 Rewrite = 0;
		uint16 VolumeLeft = 0;
		uint16 VolumeRight = 0;
		uint16 SpuPitch = 0;
		uint32 SampleStart = 0;
		uint32 LoopStart = 0;
		uint8 AttackMode = 0;
		uint8 SustainMode = 0;
		uint8 ReleaseMode = 0;
		uint8 AttackRate = 0;
		uint8 DecayRate = 0;
		uint8 SustainRate = 0;
		uint8 ReleaseRate = 0;
		uint8 SustainLevel = 0;

		uint16 Length = 0;
		uint16 Gate = 0;
		int8 LengthAdjust = 0;
		uint16 GateRatio = 0;
		uint8 PreviousKey = 0;
		uint8 Key = 0;
		uint16 Octave = 0;
		/** 16.16, in 1/256 semitone: (key * 256 + detune + tuning) << 16. */
		int32 Pitch = 0;
		int16 Tuning = 0;
		int16 Detune = 0;
		uint16 Portamento = 0;
		int16 RepeatLevel = -1;
		uint16 Pan = 0;
		uint16 Expression = 0;
		/** 8.24. */
		int32 Volume = 0;
		int32 SweepStep = 0;
		uint16 SweepTicks = 0;
		int16 SweepStart = 0;
		int32 PitchSlideStep = 0;
		int32 VolumeSlideStep = 0;
		int16 ExpressionSlideStep = 0;
		uint16 ExpressionSlideTarget = 0;
		int16 PanSlideStep = 0;
		uint16 PanSlideTarget = 0;
		uint16 PitchSlideTicks = 0;
		uint16 VolumeSlideTicks = 0;
		uint16 PanSlideTicks = 0;
		uint16 ExpressionSlideTicks = 0;
		uint16 SelectedLfo = 0;
		uint16 ActiveLfos = 0;
		int16 LfoPitch = 0;
		int16 LfoVolume = 0;
		int16 LfoPan = 0;
		std::array<FSoundRepeat, RepeatDepth> Repeats{};
		std::array<FSoundLfo, LfoCount> Lfos{};
	};

	struct FSoundBank
	{
		uint16 Id = 0;
		uint32 Address = 0;
		bool bLoaded = false;
		std::array<FSoundInstrument, FSoundBankFile::InstrumentSlots> Instruments{};
		/** Its instruments are recorded voices (LoadVoiceBank). */
		bool bVoices = false;
	};

	struct FSoundSequence
	{
		/** Null for the effect player. */
		const FSoundSequenceFile* File = nullptr;
		bool bInUse = false;
		uint16 Flags = 0;
		uint8 TrackCount = 0;
		uint16 BankId = 0;
		uint8 VariableA = 0;
		uint8 VariableB = 0;
		uint16 NoiseClock = 0;
		uint16 SeekMeasure = 0;
		uint16 Measure = 0;
		uint16 Beat = 0;
		uint16 BeatTicksLeft = 0;
		uint16 BeatsPerMeasure = 0;
		uint16 TicksPerBeat = 0;
		uint16 BeatUnit = 0;
		uint8 ReverbType = 0;
		uint8 ReverbDelay = 0;
		uint8 ReverbFeedback = 0;
		int16 ReverbDepth = 0;
		uint32 Active = 0;
		uint32 Muted = 0;
		double WaveFrame = 0.0;
		uint64 PlaybackFrames = 0;
		bool bWaveTail = false;
		int32 TempoAccumulator = 0;
		int32 TempoStep = 0;
		/** 16.16. */
		int32 Tempo = 0;
		int32 TempoSlideStep = 0;
		int16 TempoSlideCount = 0;
		uint16 TempoSlideTarget = 0;
		/** Tempo scale (16.16, 100h normal), volume (8.24), pitch and pan offsets (high halves). */
		FSoundFade Scale;
		FSoundFade Volume;
		FSoundFade Pitch;
		FSoundFade Pan;
		std::array<FSoundTrack, FSoundSequenceFile::MaxTracks> Tracks{};

		bool IsPlaying() const { return (Flags & SequenceFlag::Playing) != 0; }
		std::span<FSoundTrack> Used() { return std::span<FSoundTrack>(Tracks).first(TrackCount); }
	};

	/** A transfer into sound RAM, made when the call that asked for it returns. */
	struct FSoundTransfer
	{
		uint32 Address = 0;
		std::span<const uint8> Data;
		/** Whether it is a step of clearing the reverb's work area, which goes on when it completes. */
		bool bClearsReverb = false;
	};

	struct FSoundDriver::FState
	{
		static constexpr int32 EffectTracks = 16;
		static constexpr int32 AutomaticEffectTracks = 8;
		static constexpr int32 EffectChannels = 2;
		static constexpr int32 TransferSlots = 8;

		FState(ISpuBus& InSpu, const FSoundDriverTables& InTables, const FSoundDriverOptions& InOptions);

		ISpuBus& Spu;
		const FSoundDriverTables Tables;
		FSoundDriverOptions Options;
		FSpuLibrary Library;
		FSoundRam Ram;
		uint16 Flags = DriverFlag::Started;

		std::array<FSoundTrack*, VoiceCount> Owners{};
		/** What each voice's last note was, kept while it rings on after its track lets it go. */
		std::array<ESoundKind, VoiceCount> VoiceKinds{};
		uint32 KeyOnMask = 0;
		uint32 KeyOffMask = 0;
		uint32 ReleasedMask = 0;
		uint32 InterruptCount = 0;
		uint32 RandomState = 0x12345678;

		std::array<FSoundBank, MaxBanks> BankSlots{};
		/** Loaded banks in load order, the order the driver searches them in. */
		std::array<FSoundBank*, MaxBanks> Banks{};
		int32 BankCount = 0;
		std::array<const FSoundEffectFile*, MaxEffectFiles> EffectFiles{};
		int32 EffectFileCount = 0;
		std::array<FSoundSequence, MaxSequences> SequenceSlots{};
		FSoundSequence EffectPlayer;
		/** Sequences in the order the interrupt runs them: the newest first, the effect player last. */
		std::array<FSoundSequence*, MaxSequences + 1> Sequences{};
		int32 SequenceCount = 0;

		FSpuCommonAttributes Common;
		FSoundFade MasterFade;
		FSoundFade CdFade;
		int16 MasterVolume = 0;
		uint16 CdVolume = 0;
		uint8 ReverbType = 0xFF;
		uint8 ReverbDelay = 0;
		uint8 ReverbFeedback = 0;
		int16 ReverbDepth = 0;
		std::array<int16, 2> ReverbVolume{};
		/** Where the reverb's work area was reserved; -1 before the first. */
		int32 ReverbArea = -1;
		uint32 ClearAddress = 0;
		uint32 ClearRemaining = 0;

		std::array<FSoundTransfer, TransferSlots> Transfers{};
		int32 TransferCount = 0;

		// Voices (SoundDriverVoices.cpp).
		void ClaimVoice(FSoundTrack& Track, uint8 Voice);
		void ReleaseVoice(FSoundTrack& Track, uint8 Voice);
		void KeyOnVoice(FSoundTrack& Track, uint8 Voice);
		void KeyOffVoice(const FSoundTrack& Track, uint8 Voice);
		void UpdateVoices(FSoundSequence& Sequence);
		void FlushVoices();
		void KeyOffVoices();

		// LFOs (SoundDriverLfo.cpp).
		void StepLfos(FSoundSequence& Sequence);
		int32 RunWaveform(FSoundLfo& Lfo);
		int32 Random();

		// Banks, instruments, sequences (SoundDriverSequences.cpp).
		FSoundBank* FindBank(int32 Id) const;
		const FSoundBank* BankOrFirst(int32 Id) const;
		const FSoundBank* LoadBankSlot(const FSoundBankFile& File, int32 SpuAddress, bool bVoices);
		void SetInstrument(uint8 Number, FSoundTrack& Track);
		void Link(FSoundSequence& Sequence);
		void ResetSequence(FSoundSequence& Sequence);
		void ResetSequenceState(FSoundSequence& Sequence);
		void ResetTracks(FSoundSequence& Sequence);
		void MarkTracks(uint16 Requests, FSoundSequence& Sequence);
		void SetSequenceVolume(FSoundSequence& Sequence, int32 Volume, int32 Interrupts);
		void RestartSequence(FSoundSequence& Sequence);
		void StopSequence(FSoundSequence& Sequence);
		void SetTempoScale(FSoundSequence& Sequence, int32 Scale, int32 Interrupts);
		/** Stops every track that still reads Bytes, and forgets its places there. */
		void ForgetCommands(std::span<const uint8> Bytes);

		// The interrupt (SoundDriverInterrupt.cpp).
		void RunInterrupt();
		void StepCommonFades();
		void Advance(FSoundSequence& Sequence);
		void StepTracks(FSoundSequence& Sequence);

		// Reading commands (SoundDriverTracks.cpp, SoundCommands.cpp, SoundVoiceCommands.cpp).
		void ReadTracks(FSoundSequence& Sequence);
		FSoundCursor PlayNote(FSoundSequence& Sequence, FSoundTrack& Track, FSoundCursor At, uint16 FlagsBefore);
		void PlayPercussion(const FSoundSequence& Sequence, FSoundTrack& Track, uint8 Key);
		void LookAhead(FSoundTrack& Track, bool bPlayed);
		FSoundCursor RunCommand(uint8 Code, FSoundSequence& Sequence, FSoundTrack& Track, FSoundCursor Operands);
		FSoundCursor RunVoiceCommand(uint8 Code, FSoundSequence& Sequence, FSoundTrack& Track, FSoundCursor Operands);
		void EndTrack(FSoundTrack& Track);

		// Effects (SoundDriverEffects.cpp).
		const FSoundEffectFile* EffectFile(uint16 FileId) const;
		void StopEffectTrack(FSoundTrack& Track);
		int32 AllocateEffectPair(uint32 Id);
		void StartEffect(uint16 PairAndClass, uint32 Id, int16 Volume, int16 Pan);
		void PlayEffectVolumePan(uint32 Id, int32 Volume, int32 Pan);
		void StopEffect(uint32 Id);
		std::span<FSoundTrack> EffectPair(int32 Channel);

		// Output, reverb and transfers (SoundDriverOutput.cpp).
		void Start();
		std::array<int16, 2> OutputPair(int16 Volume, bool bReverb) const;
		void ApplyVolumes();
		void SetReverb(int32 Type, int16 Depth, int32 Delay, int32 Feedback);
		void ClearReverbStep();
		void QueueWrite(uint32 Address, std::span<const uint8> Data, bool bClearsReverb);
		void RunTransfers();
	};
}
