#include "Sound/SoundDriverMath.h"
#include "Sound/SoundDriverState.h"

namespace Weltall
{
	namespace
	{
		constexpr uint32 Bit(uint8 Voice) { return 1u << Voice; }
		/** The release a voice gets when the driver lets it go: rate 6, linear. */
		constexpr uint16 FastRelease = 6;
	}

	void FSoundDriver::FState::ClaimVoice(FSoundTrack& Track, uint8 Voice)
	{
		if (Voice >= VoiceCount)
		{
			return;
		}
		FSoundTrack* Owner = Owners[Voice];
		if (Owner == &Track)
		{
			ReleasedMask |= Bit(Voice);
		}
		else if (Owner == nullptr || Owner->Priority <= Track.Priority)
		{
			Track.Rewrite = VoiceRewrite::All;
			Owners[Voice] = &Track;
			ReleasedMask |= Bit(Voice);
			KeyOnMask &= ~Bit(Voice);
		}
	}

	void FSoundDriver::FState::ReleaseVoice(FSoundTrack& Track, uint8 Voice)
	{
		if (Voice < VoiceCount && Owners[Voice] == &Track)
		{
			Owners[Voice] = nullptr;
			ReleasedMask |= Bit(Voice);
			KeyOnMask &= ~Bit(Voice);
		}
	}

	void FSoundDriver::FState::KeyOnVoice(FSoundTrack& Track, uint8 Voice)
	{
		if (Voice >= VoiceCount)
		{
			return;
		}
		FSoundTrack* Owner = Owners[Voice];
		if (Owner != &Track)
		{
			// While a track of higher priority (an effect) holds the voice, a music note stays silent.
			if (Owner != nullptr && Track.Priority < Owner->Priority)
			{
				return;
			}
			Track.Rewrite = VoiceRewrite::All;
			Owners[Voice] = &Track;
			ReleasedMask |= Bit(Voice);
		}
		KeyOnMask |= Bit(Voice);
		VoiceKinds[Voice] = Track.Kind;
	}

	void FSoundDriver::FState::KeyOffVoice(const FSoundTrack& Track, uint8 Voice)
	{
		if (Voice < VoiceCount && Owners[Voice] == &Track)
		{
			KeyOffMask |= Bit(Voice);
		}
	}

	void FSoundDriver::FState::UpdateVoices(FSoundSequence& Sequence)
	{
		if (Sequence.Flags & SequenceFlag::Seeking)
		{
			return;
		}
		const bool bStereo = (Flags & DriverFlag::Stereo) != 0;
		for (FSoundTrack& Track : Sequence.Used())
		{
			if (Track.Flags == 0)
			{
				continue;
			}
			const uint16 Requests = Track.Requests;
			if (Requests & TrackRequest::Volume)
			{
				const SoundDriverMath::FVoiceVolumes Volumes = SoundDriverMath::VoiceVolumes(
					{Sequence.Volume.Value, Sequence.Pan.Value, Track.Volume, Track.Expression, Track.Pan, Track.LfoVolume, Track.LfoPan}, bStereo);
				Track.VolumeLeft = Volumes.Left;
				Track.VolumeRight = Volumes.Right;
				if (Sequence.File != nullptr && Sequence.File->Replacement && Sequence.Muted == 0)
				{
					Track.VolumeLeft = 0;
					Track.VolumeRight = 0;
				}
				Track.Rewrite |= VoiceRewrite::Volumes;
			}
			if (Requests & TrackRequest::Pitch)
			{
				const int16 Pitch = static_cast<int16>(static_cast<int16>(static_cast<uint32>(Track.Pitch) >> 16) + Track.LfoPitch
					+ static_cast<int16>(static_cast<uint32>(Sequence.Pitch.Value) >> 16));
				Track.SpuPitch = SoundDriverMath::PitchRegister(Tables, Pitch, Options.bHighNotesStayHigh);
				Track.Rewrite |= VoiceRewrite::Pitch;
			}
			if ((Requests & TrackRequest::KeyOn) && !(Track.Flags & TrackFlag::Muted))
			{
				KeyOnVoice(Track, Track.Voice);
			}
			if (Requests & TrackRequest::KeyOff)
			{
				KeyOffVoice(Track, Track.Voice);
			}
			Track.Requests = 0;
		}
	}

	void FSoundDriver::FState::FlushVoices()
	{
		using namespace SpuRegister;
		uint32 Modulated = 0;
		uint32 Noisy = 0;
		uint32 Reverberant = 0;
		uint16 Switches = 0;
		for (int32 Voice = 0; Voice < VoiceCount; ++Voice)
		{
			FSoundTrack* Track = Owners[static_cast<std::size_t>(Voice)];
			if (Track == nullptr)
			{
				continue;
			}
			const uint16 Rewrite = Track->Rewrite;
			if (Rewrite != 0)
			{
				if (Rewrite & VoiceRewrite::Volumes)
				{
					Spu.Write(OfVoice(Voice, VolumeLeft), Track->VolumeLeft);
					Spu.Write(OfVoice(Voice, VolumeRight), Track->VolumeRight);
				}
				if (Rewrite & VoiceRewrite::Pitch)
				{
					Spu.Write(OfVoice(Voice, SpuRegister::Pitch), Track->SpuPitch);
				}
				if (Rewrite & VoiceRewrite::Addresses)
				{
					Spu.Write(OfVoice(Voice, StartAddress), static_cast<uint16>(Track->SampleStart >> 3));
					Spu.Write(OfVoice(Voice, LoopAddress), static_cast<uint16>(Track->LoopStart >> 3));
				}
				const uint32 Envelope1 = OfVoice(Voice, Adsr1);
				const uint32 Envelope2 = OfVoice(Voice, Adsr2);
				if (Rewrite & VoiceRewrite::Attack)
				{
					Spu.Write(Envelope1, SoundDriverMath::WithAttack(Spu.Read(Envelope1), Track->AttackRate, Track->AttackMode));
				}
				if (Rewrite & VoiceRewrite::Decay)
				{
					Spu.Write(Envelope1, SoundDriverMath::WithDecay(Spu.Read(Envelope1), Track->DecayRate));
				}
				if (Rewrite & VoiceRewrite::SustainRate)
				{
					Spu.Write(Envelope2, SoundDriverMath::WithSustainRate(Spu.Read(Envelope2), Track->SustainRate, Track->SustainMode));
				}
				if (Rewrite & VoiceRewrite::Release)
				{
					Spu.Write(Envelope2, SoundDriverMath::WithRelease(Spu.Read(Envelope2), Track->ReleaseRate, Track->ReleaseMode));
				}
				if (Rewrite & VoiceRewrite::SustainLevel)
				{
					Spu.Write(Envelope1, SoundDriverMath::WithSustainLevel(Spu.Read(Envelope1), Track->SustainLevel));
				}
				Switches |= Rewrite & VoiceRewrite::Switches;
				Track->Rewrite = 0;
			}
			Modulated |= static_cast<uint32>((Track->Switches & VoiceSwitch::PitchModulation) != 0) << Voice;
			Noisy |= static_cast<uint32>((Track->Switches & VoiceSwitch::Noise) != 0) << Voice;
			Reverberant |= static_cast<uint32>((Track->Switches & VoiceSwitch::Reverb) != 0) << Voice;
		}
		const auto WriteMask = [this](uint32 Register, uint32 Mask) {
			Spu.Write(Register, static_cast<uint16>(Mask));
			Spu.Write(Register + 2, static_cast<uint16>(Mask >> 16));
		};
		if (Switches & VoiceRewrite::PitchModulation)
		{
			WriteMask(PitchModulation, Modulated);
		}
		if (Switches & VoiceRewrite::Noise)
		{
			WriteMask(Noise, Noisy);
		}
		if (Switches & VoiceRewrite::Reverb)
		{
			WriteMask(SpuRegister::Reverb, Reverberant);
		}
		if (KeyOnMask != 0)
		{
			WriteMask(KeyOn, KeyOnMask);
			KeyOnMask = 0;
		}
	}

	void FSoundDriver::FState::KeyOffVoices()
	{
		using namespace SpuRegister;
		if (ReleasedMask != 0)
		{
			for (uint8 Voice = 0; Voice < VoiceCount; ++Voice)
			{
				if (ReleasedMask & Bit(Voice))
				{
					const uint32 Envelope2 = OfVoice(Voice, Adsr2);
					Spu.Write(Envelope2, static_cast<uint16>((Spu.Read(Envelope2) & 0xFFC0) | FastRelease));
				}
			}
		}
		const uint32 Mask = KeyOffMask | ReleasedMask;
		if (Mask != 0)
		{
			Spu.Write(KeyOff, static_cast<uint16>(Mask));
			Spu.Write(KeyOff + 2, static_cast<uint16>(Mask >> 16));
			ReleasedMask = 0;
			KeyOffMask = 0;
		}
	}
}
