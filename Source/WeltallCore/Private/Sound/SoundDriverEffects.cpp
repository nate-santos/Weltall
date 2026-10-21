#include "Sound/SoundDriverIntegers.h"
#include "Sound/SoundDriverState.h"

namespace Weltall
{
	using namespace SoundInteger;

	namespace
	{
		constexpr int16 EffectPriority = 0x200;
		constexpr uint16 FirstRequests = 0x170;
		constexpr uint16 MiddleOctave = 0x3C;
		constexpr uint16 DefaultGateRatio = 0x0F;
		constexpr int32 FullVolume = 0x7F000000;
		constexpr int16 DefaultVolume = 0x6000;
		constexpr int16 CentrePan = 0x4000;
		constexpr uint8 NormalClass = 0x20;
		constexpr uint16 NormalEffect = 0x2000;
		constexpr uint16 SystemEffect = 0x8000;
		/** Pair 12 at class 60h: above the automatic effects, so none of them takes its channels. */
		constexpr uint16 ProtectedEffect = 0x600C;

		/** An effect's volume in a play call's byte form, as the expression's high byte. */
		int16 ByteLevel(int32 Value)
		{
			return static_cast<int16>(static_cast<int8>(Value) * 0x100);
		}
	}

	const FSoundEffectFile* FSoundDriver::FState::EffectFile(uint16 FileId) const
	{
		for (int32 Index = 0; Index < EffectFileCount; ++Index)
		{
			if (EffectFiles[static_cast<std::size_t>(Index)]->FileId() == FileId)
			{
				return EffectFiles[static_cast<std::size_t>(Index)];
			}
		}
		return nullptr;
	}

	std::span<FSoundTrack> FSoundDriver::FState::EffectPair(int32 Channel)
	{
		const int32 First = (Channel & 0xFE) ^ 8;
		if (First < 0 || First + EffectChannels > EffectTracks)
		{
			return {};
		}
		return std::span<FSoundTrack>(EffectPlayer.Tracks).subspan(static_cast<std::size_t>(First), EffectChannels);
	}

	void FSoundDriver::FState::StopEffectTrack(FSoundTrack& Track)
	{
		Track.Flags = 0;
		EffectPlayer.Active &= ~(1u << Track.Number);
		ReleaseVoice(Track, Track.Voice);
	}

	int32 FSoundDriver::FState::AllocateEffectPair(uint32 Id)
	{
		for (FSoundTrack& Track : EffectPlayer.Used())
		{
			if ((Track.Flags & TrackFlag::Active) && Track.EffectId == Id)
			{
				StopEffectTrack(Track);
			}
		}
		// The first free pair among 12, 10 and 8, else the one started longest ago among those
		// normal effects hold.
		constexpr uint32 PairMask = (1u << EffectChannels) - 1;
		int32 Pair = EffectTracks - (EffectChannels + 2);
		uint32 Mask = PairMask << Pair;
		if ((EffectPlayer.Active & Mask) == 0)
		{
			return Pair;
		}
		uint32 Oldest = 0xFFFFFFFF;
		int32 Chosen = -1;
		while (true)
		{
			const FSoundTrack& Track = EffectPlayer.Tracks[static_cast<std::size_t>(Pair)];
			Mask >>= EffectChannels;
			if (Track.EffectTime < Oldest && Track.EffectClass <= NormalClass)
			{
				Oldest = Track.EffectTime;
				Chosen = Pair;
			}
			if (Mask < PairMask || Pair <= EffectTracks - AutomaticEffectTracks)
			{
				break;
			}
			Pair -= EffectChannels;
			if ((EffectPlayer.Active & Mask) == 0)
			{
				return Pair;
			}
		}
		if (Chosen >= 0)
		{
			return Chosen;
		}
		// Every pair holds a protected effect: the original returns whatever a register held. The
		// port takes the pair started longest ago.
		Chosen = EffectTracks - (EffectChannels + 2);
		for (int32 Candidate = Chosen - EffectChannels; Candidate >= EffectTracks - AutomaticEffectTracks; Candidate -= EffectChannels)
		{
			if (EffectPlayer.Tracks[static_cast<std::size_t>(Candidate)].EffectTime < EffectPlayer.Tracks[static_cast<std::size_t>(Chosen)].EffectTime)
			{
				Chosen = Candidate;
			}
		}
		return Chosen;
	}

	void FSoundDriver::FState::StartEffect(uint16 PairAndClass, uint32 Id, int16 Volume, int16 Pan)
	{
		const int32 FileId = static_cast<int32>(Id) >> 16;
		const FSoundEffectFile* File = FileId >= 0 ? EffectFile(static_cast<uint16>(FileId)) : nullptr;
		const int32 Number = static_cast<int32>(Id & 0xFFFF);
		const int32 First = PairAndClass & 0xFF;
		// An effect number past the file's table reads its commands' bytes as offsets in the original.
		if (File == nullptr || Number >= File->EffectCount() || First + EffectChannels > EffectTracks)
		{
			return;
		}
		const FSoundBank* Bank = BankOrFirst(File->BankId());
		// The effect's volume scales the call's; a product past the expression's range saturates.
		const uint32 Scaled = static_cast<uint32>(Volume * File->Volume(Number));
		const uint16 Expression = (Scaled >> 22 & 1) ? 0x7FFF : static_cast<uint16>(Scaled >> 7);
		for (int32 Channel = 0; Channel < EffectChannels; ++Channel)
		{
			FSoundTrack& Track = EffectPlayer.Tracks[static_cast<std::size_t>(First + Channel)];
			Track.EffectId = Id;
			Track.EffectTime = InterruptCount;
			Track.EffectClass = static_cast<uint8>(PairAndClass >> 8);
			Track.Kind = ESoundKind::Effect;
			const uint16 Offset = File->ChannelOffset(Number * EffectChannels + Channel);
			if (Offset == 0)
			{
				EffectPlayer.Active &= ~(1u << Track.Number);
				Track.Flags = 0;
				ReleaseVoice(Track, Track.Voice);
				continue;
			}
			EffectPlayer.Active |= 1u << Track.Number;
			Track.Flags = static_cast<uint16>(TrackFlag::Active | TrackFlag::IgnoreVelocity | TrackFlag::Released
				| ((File->Flags() & 1) ? TrackFlag::FromEffectFile : 0));
			Track.Requests = FirstRequests;
			Track.Slides = 0;
			Track.Octave = MiddleOctave;
			Track.GateRatio = DefaultGateRatio;
			Track.RepeatLevel = -1;
			Track.LoopPoint = {};
			Track.Loops = 0;
			Track.Length = 0;
			Track.LengthAdjust = 0;
			Track.Detune = 0;
			Track.PreviousKey = 0;
			Track.Expression = Expression;
			Track.Volume = FullVolume;
			Track.Portamento = 0;
			Track.LfoPitch = 0;
			Track.LfoVolume = 0;
			Track.LfoPan = 0;
			Track.ActiveLfos = 0;
			Track.Pan = static_cast<uint16>(Pan);
			Track.Current = FSoundCursor::In(File->Bytes(), Offset);
			for (FSoundLfo& Lfo : Track.Lfos)
			{
				Lfo.Flags = 0;
			}
			Track.Bank = Bank;
			Track.BankId = static_cast<uint8>(File->BankId());
			if (Bank != nullptr)
			{
				SetInstrument(0, Track);
			}
			Track.Switches = 0;
			Track.Priority = EffectPriority;
			ClaimVoice(Track, Track.Voice);
		}
		EffectPlayer.Flags |= SequenceFlag::Playing;
	}

	void FSoundDriver::FState::PlayEffectVolumePan(uint32 Id, int32 Volume, int32 Pan)
	{
		if (Flags & DriverFlag::EffectsEnabled)
		{
			StartEffect(static_cast<uint16>(AllocateEffectPair(Id) | NormalEffect), Id, ByteLevel(Volume), ByteLevel(Pan));
		}
	}

	void FSoundDriver::FState::StopEffect(uint32 Id)
	{
		for (FSoundTrack& Track : EffectPlayer.Used())
		{
			if ((Track.Flags & TrackFlag::Active) && Track.EffectId == Id)
			{
				StopEffectTrack(Track);
			}
		}
	}

	void FSoundDriver::PlayEffect(uint32 Id)
	{
		if (State->Flags & DriverFlag::EffectsEnabled)
		{
			State->StartEffect(static_cast<uint16>(State->AllocateEffectPair(Id) | NormalEffect), Id, DefaultVolume, CentrePan);
		}
	}

	void FSoundDriver::PlayEffectVolumePan(uint32 Id, int32 Volume, int32 Pan)
	{
		State->PlayEffectVolumePan(Id, Volume, Pan);
	}

	void FSoundDriver::PlayEffectOn(uint32 Id, int32 Channel)
	{
		if (State->Flags & DriverFlag::EffectsEnabled)
		{
			State->StartEffect(static_cast<uint16>(((Channel & 0xFE) ^ 8) | NormalEffect), Id, DefaultVolume, CentrePan);
		}
	}

	void FSoundDriver::PlayEffectOnVolumePan(uint32 Id, int32 Channel, int32 Volume, int32 Pan)
	{
		if (State->Flags & DriverFlag::EffectsEnabled)
		{
			State->StartEffect(static_cast<uint16>(((Channel & 0xFE) ^ 8) | NormalEffect), Id, ByteLevel(Volume), ByteLevel(Pan));
		}
	}

	void FSoundDriver::PlaySystemEffect(uint32 Id)
	{
		if (State->Flags & DriverFlag::EffectsEnabled)
		{
			State->StartEffect(static_cast<uint16>((FState::EffectTracks - FState::EffectChannels) | SystemEffect), Id, DefaultVolume, CentrePan);
		}
	}

	void FSoundDriver::PlayProtectedEffect(uint32 Id)
	{
		if (State->Flags & DriverFlag::EffectsEnabled)
		{
			State->StartEffect(ProtectedEffect, Id, DefaultVolume, CentrePan);
		}
	}

	void FSoundDriver::StopEffect(uint32 Id)
	{
		State->StopEffect(Id);
	}

	void FSoundDriver::StopEffectOn(int32 Channel)
	{
		for (FSoundTrack& Track : State->EffectPair(Channel))
		{
			if (Track.Flags & TrackFlag::Active)
			{
				State->StopEffectTrack(Track);
			}
		}
	}

	void FSoundDriver::StopEffectsOfFile(const FSoundEffectFile& File)
	{
		for (FSoundTrack& Track : State->EffectPlayer.Used())
		{
			if ((Track.Flags & TrackFlag::Active) && static_cast<uint16>(Track.EffectId >> 16) == File.FileId())
			{
				State->StopEffectTrack(Track);
			}
		}
	}

	void FSoundDriver::StopAllEffects()
	{
		for (FSoundTrack& Track : State->EffectPlayer.Used())
		{
			if (Track.Flags & TrackFlag::Active)
			{
				Track.Flags = 0;
				State->ReleaseVoice(Track, Track.Voice);
			}
		}
		State->EffectPlayer.Active = 0;
	}

	void FSoundDriver::SetEffectVolume(uint32 Id, int32 Volume)
	{
		for (FSoundTrack& Track : State->EffectPlayer.Used())
		{
			if ((Track.Flags & TrackFlag::Active) && Track.EffectId == Id)
			{
				Track.Expression = static_cast<uint16>(Volume << 8);
				Track.Requests = TrackRequest::Volume;
			}
		}
	}

	void FSoundDriver::SetEffectVolumeOn(int32 Channel, int32 Volume)
	{
		for (FSoundTrack& Track : State->EffectPair(Channel))
		{
			if (Track.Flags & TrackFlag::Active)
			{
				Track.Expression = static_cast<uint16>(Volume << 8);
				Track.Requests = TrackRequest::Volume;
			}
		}
	}

	void FSoundDriver::SetEffectPanOn(int32 Channel, int32 Pan)
	{
		for (FSoundTrack& Track : State->EffectPair(Channel))
		{
			if (Track.Flags & TrackFlag::Active)
			{
				Track.Pan = static_cast<uint16>(Pan << 8);
				Track.Requests = TrackRequest::Volume;
			}
		}
	}

	namespace
	{
		/** An expression slide to Volume over Ticks (at least one) of the effect's own. */
		void FadeExpression(FSoundTrack& Track, int32 Volume, int32& Ticks)
		{
			const int32 Difference = Volume * 0x100 - static_cast<int16>(Track.Expression);
			if (Difference == 0)
			{
				return;
			}
			if (Ticks == 0)
			{
				Ticks = 1;
			}
			Track.ExpressionSlideTarget = static_cast<uint16>(Volume * 0x100);
			Track.ExpressionSlideTicks = static_cast<uint16>(Ticks);
			Track.Slides |= TrackSlide::Expression;
			Track.ExpressionSlideStep = static_cast<int16>(Divide(Difference, Ticks));
		}
	}

	void FSoundDriver::FadeEffectVolume(uint32 Id, int32 Volume, int32 Ticks)
	{
		for (FSoundTrack& Track : State->EffectPlayer.Used())
		{
			if ((Track.Flags & TrackFlag::Active) && Track.EffectId == Id)
			{
				FadeExpression(Track, Volume, Ticks);
			}
		}
	}

	void FSoundDriver::FadeEffectVolumeOn(int32 Channel, int32 Volume, int32 Ticks)
	{
		for (FSoundTrack& Track : State->EffectPair(Channel))
		{
			if (Track.Flags & TrackFlag::Active)
			{
				FadeExpression(Track, Volume, Ticks);
			}
		}
	}

	uint32 FSoundDriver::PlayingEffects(uint32 Id) const
	{
		uint32 Mask = 0;
		for (const FSoundTrack& Track : State->EffectPlayer.Used())
		{
			if ((Track.Flags & TrackFlag::Active) && (Id == AnyEffect || Track.EffectId == Id))
			{
				Mask |= 1u << Track.Number;
			}
		}
		return Mask;
	}
}
