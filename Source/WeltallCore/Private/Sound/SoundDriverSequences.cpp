#include "Sound/SoundDriverIntegers.h"
#include "Sound/SoundDriverState.h"

namespace Weltall
{
	using namespace SoundInteger;

	namespace
	{
		constexpr int16 MusicPriority = 0x100;
		constexpr uint8 MusicClass = 0x10;
		constexpr uint16 FirstRequests = 0x170;
		constexpr uint16 MiddleOctave = 0x3C;
		constexpr uint16 DefaultGateRatio = 0x0F;
		constexpr uint16 CentrePan = 0x4000;
		constexpr uint16 DefaultExpression = 0x6000;
		constexpr int32 FullVolume = 0x7F000000;
		constexpr int32 NormalScale = 0x100;

		/** Sets a fade toward Value (as its high half's high byte for 8.24 values) over Interrupts, or at once. */
		bool StartFade(FSoundFade& Fade, int32 Value, int32 Interrupts)
		{
			Fade.Target = static_cast<int16>(Value << 8);
			if (Interrupts == 0)
			{
				Fade.Value = ShiftLeft(Value, 24);
				Fade.Count = 0;
				return true;
			}
			const int32 Difference = Subtract(Value * 0x10000, Fade.Value >> 8);
			if (Difference != 0)
			{
				Fade.Count = static_cast<int16>(Interrupts);
				Fade.Step = ShiftLeft(Divide(Difference, Interrupts), 8);
			}
			return false;
		}
	}

	FSoundBank* FSoundDriver::FState::FindBank(int32 Id) const
	{
		for (int32 Index = 0; Index < BankCount; ++Index)
		{
			if (Banks[static_cast<std::size_t>(Index)]->Id == Id)
			{
				return Banks[static_cast<std::size_t>(Index)];
			}
		}
		return nullptr;
	}

	const FSoundBank* FSoundDriver::FState::BankOrFirst(int32 Id) const
	{
		const FSoundBank* Bank = FindBank(Id);
		return Bank != nullptr ? Bank : (BankCount != 0 ? Banks[0] : nullptr);
	}

	const FSoundBank* FSoundDriver::FState::LoadBankSlot(const FSoundBankFile& File, int32 SpuAddress, bool bVoices)
	{
		FSoundBank* Slot = nullptr;
		for (FSoundBank& Candidate : BankSlots)
		{
			if (!Candidate.bLoaded)
			{
				Slot = &Candidate;
				break;
			}
		}
		if (Slot == nullptr)
		{
			return nullptr;
		}
		const uint32 Wanted = SpuAddress == 0 ? File.RequestedAddress() : (SpuAddress == -1 ? 0 : static_cast<uint32>(SpuAddress));
		const uint32 Bytes = static_cast<uint32>(File.Samples().size());
		const uint32 Address = Wanted == 0 ? Ram.Reserve(Bytes) : Ram.ReserveAt(Bytes, File.RequestedAddress());
		if (Address == 0)
		{
			return nullptr;
		}
		QueueWrite(Address, File.Samples(), false);
		// The driver keeps its own copy of the header: a freed bank's instruments stay readable to the
		// tracks still pointing at them until another bank takes the place, as in the original's heap.
		*Slot = FSoundBank{File.Id(), Address, true, {}, bVoices};
		const auto Instruments = File.Instruments();
		std::copy(Instruments.begin(), Instruments.end(), Slot->Instruments.begin());
		Banks[static_cast<std::size_t>(BankCount++)] = Slot;
		RunTransfers();
		return Slot;
	}

	void FSoundDriver::FState::SetInstrument(uint8 Number, FSoundTrack& Track)
	{
		Track.Instrument = Number;
		if (Track.Kind != ESoundKind::Music)
		{
			Track.Kind = Track.Bank != nullptr && Track.Bank->bVoices ? ESoundKind::Voice : ESoundKind::Effect;
		}
		const FSoundInstrument Instrument = Track.Bank != nullptr ? Track.Bank->Instruments[Number] : FSoundInstrument{};
		const uint32 BankAddress = Track.Bank != nullptr ? Track.Bank->Address : 0;
		Track.SampleStart = Instrument.SampleStart * 8 + BankAddress;
		// The loop address leaves out the bank's place in sound RAM; the SPU never uses it, since every
		// looping sample marks its own loop start.
		Track.LoopStart = Instrument.SampleStart * 8 + Instrument.LoopStart * 8u;
		Track.AttackMode = Instrument.Modes & 7;
		Track.SustainMode = Instrument.Modes >> 4 & 7;
		Track.ReleaseMode = Instrument.Modes >> 8 & 7;
		Track.AttackRate = Instrument.Rates & 0x7F;
		Track.DecayRate = Instrument.Rates >> 8 & 0xF;
		Track.SustainRate = Instrument.Rates >> 16 & 0x7F;
		Track.SustainLevel = Instrument.Rates >> 12 & 0xF;
		Track.ReleaseRate = Instrument.Rates >> 24 & 0x1F;
		Track.InstrumentRelease = Track.ReleaseRate;
		Track.Flags |= TrackFlag::InstrumentChanged;
		Track.Tuning = Instrument.Tuning;
	}

	void FSoundDriver::FState::Link(FSoundSequence& Sequence)
	{
		for (int32 Index = SequenceCount; Index > 0; --Index)
		{
			Sequences[static_cast<std::size_t>(Index)] = Sequences[static_cast<std::size_t>(Index - 1)];
		}
		Sequences[0] = &Sequence;
		++SequenceCount;
	}

	void FSoundDriver::FState::ResetSequence(FSoundSequence& Sequence)
	{
		const FSoundSequenceFile& File = *Sequence.File;
		Sequence.Flags |= SequenceFlag::HasFile;
		Sequence.TrackCount = static_cast<uint8>(File.TrackCount());
		Sequence.BankId = File.BankId();
		Sequence.ReverbType = File.ReverbType();
		Sequence.ReverbDepth = static_cast<int16>(File.ReverbDepth() << 8);
		Sequence.ReverbDelay = File.ReverbDelay();
		Sequence.ReverbFeedback = File.ReverbFeedback();
		if (Flags & DriverFlag::SequencesSetReverb)
		{
			SetReverb(Sequence.ReverbType, Sequence.ReverbDepth, Sequence.ReverbDelay, Sequence.ReverbFeedback);
		}
		ResetSequenceState(Sequence);
	}

	void FSoundDriver::FState::ResetSequenceState(FSoundSequence& Sequence)
	{
		Sequence.Measure = 1;
		Sequence.BeatTicksLeft = 1;
		Sequence.TicksPerBeat = 0x30;
		Sequence.Scale.Value = 0x1000000;
		Sequence.Volume.Value = FullVolume;
		Sequence.Tempo = 0x660000;
		Sequence.TempoStep = 0x6600;
		Sequence.VariableA = 0;
		Sequence.VariableB = 0;
		Sequence.Beat = 0;
		Sequence.BeatsPerMeasure = 4;
		Sequence.BeatUnit = 4;
		Sequence.Active = 0;
		Sequence.Pitch.Value = 0;
		Sequence.Pan.Value = 0;
		Sequence.Scale.Count = 0;
		Sequence.Volume.Count = 0;
		Sequence.Pitch.Count = 0;
		Sequence.Pan.Count = 0;
		Sequence.TempoSlideStep = 0;
		Sequence.TempoSlideCount = 0;
		Sequence.TempoAccumulator = 0x10000;
		Sequence.WaveFrame = 0.0;
		Sequence.PlaybackFrames = 0;
		Sequence.bWaveTail = false;
	}

	void FSoundDriver::FState::ResetTracks(FSoundSequence& Sequence)
	{
		const FSoundSequenceFile& File = *Sequence.File;
		const FSoundBank* Bank = BankOrFirst(static_cast<int16>(Sequence.BankId));
		uint32 Active = 0;
		for (int32 Number = 0; Number < Sequence.TrackCount; ++Number)
		{
			FSoundTrack& Track = Sequence.Tracks[static_cast<std::size_t>(Number)];
			const uint16 Offset = File.TrackOffset(Number);
			if (Offset == 0)
			{
				Track.Flags = 0;
				continue;
			}
			Active |= 1u << Number;
			Track.Flags = static_cast<uint16>(TrackFlag::Active | TrackFlag::Released | ((Sequence.Muted >> Number & 1) ? TrackFlag::Muted : 0));
			// Sequence flag 4 carries over to the tracks.
			Track.Flags |= Sequence.Flags & 0x0004;
			Track.Requests = FirstRequests;
			Track.Slides = 0;
			Track.EffectClass = MusicClass;
			Track.Kind = ESoundKind::Music;
			Track.Number = static_cast<uint8>(Number);
			Track.EffectId = File.Id();
			Track.Octave = MiddleOctave;
			Track.GateRatio = DefaultGateRatio;
			Track.RepeatLevel = -1;
			Track.Expression = DefaultExpression;
			Track.Volume = FullVolume;
			Track.LoopPoint = {};
			Track.Loops = 0;
			Track.FirstLoopFrame = 0;
			Track.FirstLoopEndFrame = 0;
			Track.Length = 0;
			Track.LengthAdjust = 0;
			Track.Detune = 0;
			Track.PreviousKey = 0;
			Track.Pan = CentrePan;
			Track.Portamento = 0;
			Track.LfoPitch = 0;
			Track.LfoVolume = 0;
			Track.LfoPan = 0;
			Track.ActiveLfos = 0;
			Track.Current = FSoundCursor::In(File.Bytes(), Offset);
			for (FSoundLfo& Lfo : Track.Lfos)
			{
				Lfo.Flags = 0;
			}
			Track.Bank = Bank;
			Track.BankId = static_cast<uint8>(Sequence.BankId);
			if (Bank != nullptr)
			{
				SetInstrument(0, Track);
			}
			// Track 0 conducts; track n plays on voice n - 1.
			Track.Voice = static_cast<uint8>(Number - 1);
			Track.Switches = 0;
			Track.Priority = MusicPriority;
			ClaimVoice(Track, Track.Voice);
		}
		Sequence.Active = Active;
	}

	void FSoundDriver::FState::MarkTracks(uint16 Requests, FSoundSequence& Sequence)
	{
		for (FSoundTrack& Track : Sequence.Used())
		{
			if (Track.Flags != 0)
			{
				Track.Requests |= Requests;
			}
		}
	}

	void FSoundDriver::FState::SetSequenceVolume(FSoundSequence& Sequence, int32 Volume, int32 Interrupts)
	{
		Sequence.Volume.Target = static_cast<int16>(Volume << 8);
		if (Interrupts == 0)
		{
			Sequence.Volume.Value = ShiftLeft(Volume, 24);
			Sequence.Volume.Count = 0;
			MarkTracks(TrackRequest::Volume, Sequence);
		}
		else
		{
			const int32 Difference = Subtract(Volume * 0x10000, Sequence.Volume.Value >> 8);
			if (Difference == 0)
			{
				return;
			}
			Sequence.Volume.Count = static_cast<int16>(Interrupts);
			Sequence.Volume.Step = ShiftLeft(Divide(Difference, Interrupts), 8);
		}
		if ((Sequence.Flags & SequenceFlag::WaitsForVolume) && Volume != 0)
		{
			RestartSequence(Sequence);
		}
	}

	void FSoundDriver::FState::RestartSequence(FSoundSequence& Sequence)
	{
		if (Flags & DriverFlag::SequencesSetReverb)
		{
			SetReverb(Sequence.ReverbType, Sequence.ReverbDepth, Sequence.ReverbDelay, Sequence.ReverbFeedback);
		}
		for (FSoundTrack& Track : Sequence.Used())
		{
			if (Track.Flags != 0)
			{
				Track.Rewrite = VoiceRewrite::All;
			}
		}
		// Notes still held key on again.
		for (FSoundTrack& Track : Sequence.Used())
		{
			if ((Track.Flags & (TrackFlag::Active | TrackFlag::StepDone)) == (TrackFlag::Active | TrackFlag::StepDone)
				&& (Track.Flags & (TrackFlag::Percussion | TrackFlag::Muted)) == 0)
			{
				Track.Requests |= TrackRequest::KeyOn;
			}
		}
		Sequence.Flags = static_cast<uint16>((Sequence.Flags & ~SequenceFlag::WaitsForVolume) | SequenceFlag::Playing);
	}

	void FSoundDriver::FState::StopSequence(FSoundSequence& Sequence)
	{
		Sequence.Flags &= static_cast<uint16>(~SequenceFlag::Playing);
		Sequence.bWaveTail = false;
		for (FSoundTrack& Track : Sequence.Used())
		{
			ReleaseVoice(Track, Track.Voice);
		}
	}

	void FSoundDriver::FState::SetTempoScale(FSoundSequence& Sequence, int32 Scale, int32 Interrupts)
	{
		if (Scale == 0)
		{
			Scale = NormalScale;
		}
		Sequence.Scale.Target = static_cast<int16>(Scale);
		if (Interrupts == 0)
		{
			Sequence.Scale.Count = 0;
			Sequence.Scale.Value = ShiftLeft(Scale, 16);
			Sequence.TempoStep = High(Sequence.Tempo) * Scale;
		}
		else
		{
			const int32 Difference = Subtract(Scale * 0x10000, Sequence.Scale.Value);
			if (Difference != 0)
			{
				Sequence.Scale.Count = static_cast<int16>(Interrupts);
				Sequence.Scale.Step = Divide(Difference, Interrupts);
			}
		}
	}

	void FSoundDriver::FState::ForgetCommands(std::span<const uint8> Bytes)
	{
		for (int32 Index = 0; Index < SequenceCount; ++Index)
		{
			FSoundSequence& Sequence = *Sequences[static_cast<std::size_t>(Index)];
			for (FSoundTrack& Track : Sequence.Used())
			{
				if (Track.Flags != 0 && (Track.Current.Points(Bytes) || Track.LoopPoint.Points(Bytes)))
				{
					EndTrack(Track);
					Sequence.Active &= ~(1u << Track.Number);
				}
				if (Track.LoopPoint.Points(Bytes))
				{
					Track.LoopPoint = {};
				}
				for (FSoundRepeat& Repeat : Track.Repeats)
				{
					Repeat.Start = Repeat.Start.Points(Bytes) ? FSoundCursor{} : Repeat.Start;
					Repeat.Exit = Repeat.Exit.Points(Bytes) ? FSoundCursor{} : Repeat.Exit;
				}
			}
		}
	}

	FSoundSequence* FSoundDriver::LoadSequence(const FSoundSequenceFile& File)
	{
		FSoundSequence* Slot = nullptr;
		for (FSoundSequence& Candidate : State->SequenceSlots)
		{
			if (!Candidate.bInUse)
			{
				Slot = &Candidate;
				break;
			}
		}
		if (Slot == nullptr)
		{
			return nullptr;
		}
		// A new sequence starts from cleared memory, as the driver's allocations do.
		*Slot = FSoundSequence{};
		Slot->bInUse = true;
		Slot->File = &File;
		State->ResetSequence(*Slot);
		State->ResetTracks(*Slot);
		Slot->Muted = 0;
		State->Link(*Slot);
		State->RunTransfers();
		return Slot;
	}

	FSoundSequence* FSoundDriver::StartSequence(const FSoundSequenceFile& File, int32 Volume, int32 Interrupts)
	{
		FSoundSequence* Sequence = LoadSequence(File);
		if (Sequence != nullptr)
		{
			PlaySequence(*Sequence, Volume, Interrupts);
		}
		return Sequence;
	}

	void FSoundDriver::PlaySequence(FSoundSequence& Sequence, int32 Volume, int32 Interrupts)
	{
		Sequence.Flags &= static_cast<uint16>(~SequenceFlag::Playing);
		State->ResetSequence(Sequence);
		State->ResetTracks(Sequence);
		Sequence.Volume.Value = 0;
		State->SetSequenceVolume(Sequence, Volume, Interrupts);
		Sequence.Flags |= SequenceFlag::Playing;
		State->RunTransfers();
	}

	void FSoundDriver::StopSequence(FSoundSequence& Sequence)
	{
		State->StopSequence(Sequence);
	}

	void FSoundDriver::FreeSequence(FSoundSequence& Sequence)
	{
		// Every voice the sequence still holds is let go: the original leaves a stopped but unplayed
		// sequence's claims pointing into the freed block.
		State->StopSequence(Sequence);
		for (int32 Index = 0; Index < State->SequenceCount; ++Index)
		{
			if (State->Sequences[static_cast<std::size_t>(Index)] == &Sequence)
			{
				for (int32 Next = Index; Next + 1 < State->SequenceCount; ++Next)
				{
					State->Sequences[static_cast<std::size_t>(Next)] = State->Sequences[static_cast<std::size_t>(Next + 1)];
				}
				--State->SequenceCount;
				break;
			}
		}
		Sequence.bInUse = false;
	}

	void FSoundDriver::StopAllSequences()
	{
		for (int32 Index = 0; Index < State->SequenceCount; ++Index)
		{
			FSoundSequence& Sequence = *State->Sequences[static_cast<std::size_t>(Index)];
			if (Sequence.Flags & SequenceFlag::HasFile)
			{
				State->StopSequence(Sequence);
			}
		}
	}

	void FSoundDriver::ResumeSequence(FSoundSequence& Sequence, int32 Volume, int32 Interrupts)
	{
		for (FSoundTrack& Track : Sequence.Used())
		{
			// Banks may have moved while the sequence was stopped; without its bank, a track reads empty instruments.
			Track.Bank = State->FindBank(Track.BankId);
			const FSoundInstrument Instrument = Track.Bank != nullptr ? Track.Bank->Instruments[Track.Instrument] : FSoundInstrument{};
			const uint32 BankAddress = Track.Bank != nullptr ? Track.Bank->Address : 0;
			Track.SampleStart = Instrument.SampleStart * 8 + BankAddress;
			Track.Rewrite = VoiceRewrite::All;
			Track.LoopStart = Instrument.SampleStart * 8 + Instrument.LoopStart * 8u;
		}
		Sequence.Volume.Value = 0;
		Sequence.Flags |= SequenceFlag::WaitsForVolume;
		State->SetSequenceVolume(Sequence, Volume, Interrupts);
		State->RunTransfers();
	}

	void FSoundDriver::SetSequenceVolume(FSoundSequence& Sequence, int32 Volume, int32 Interrupts)
	{
		State->SetSequenceVolume(Sequence, Volume, Interrupts);
		State->RunTransfers();
	}

	void FSoundDriver::SetSequencePitch(FSoundSequence& Sequence, int32 Semitones, int32 Interrupts)
	{
		if (StartFade(Sequence.Pitch, Semitones, Interrupts))
		{
			State->MarkTracks(TrackRequest::Pitch, Sequence);
		}
	}

	void FSoundDriver::SetSequencePan(FSoundSequence& Sequence, int32 Offset, int32 Interrupts)
	{
		if (StartFade(Sequence.Pan, Offset, Interrupts))
		{
			State->MarkTracks(TrackRequest::Volume, Sequence);
		}
	}

	void FSoundDriver::SetTempoScale(FSoundSequence& Sequence, int32 Scale, int32 Interrupts)
	{
		State->SetTempoScale(Sequence, Scale, Interrupts);
	}

	void FSoundDriver::MuteTracks(FSoundSequence& Sequence, uint32 Mask)
	{
		Sequence.Muted = Mask;
		if (Sequence.File != nullptr && Sequence.File->Replacement)
		{
			State->MarkTracks(TrackRequest::Volume, Sequence);
		}
		for (FSoundTrack& Track : Sequence.Used())
		{
			if (Track.Flags != 0)
			{
				if (Mask & 1)
				{
					if (!(Track.Flags & TrackFlag::Muted))
					{
						Track.Flags |= TrackFlag::Muted;
						if (Sequence.IsPlaying())
						{
							State->KeyOffVoice(Track, Track.Voice);
						}
					}
				}
				else if (Track.Flags & TrackFlag::Muted)
				{
					Track.Flags &= static_cast<uint16>(~TrackFlag::Muted);
					// A note still held when the track comes back sounds again.
					if ((Track.Flags & (TrackFlag::StepDone | TrackFlag::Percussion)) == TrackFlag::StepDone && Sequence.IsPlaying())
					{
						State->KeyOnVoice(Track, Track.Voice);
					}
				}
			}
			Mask >>= 1;
		}
	}

	void FSoundDriver::SeekSequence(FSoundSequence& Sequence, uint16 Measure)
	{
		Sequence.SeekMeasure = Measure;
		Sequence.Flags |= SequenceFlag::Seeking;
	}
}
