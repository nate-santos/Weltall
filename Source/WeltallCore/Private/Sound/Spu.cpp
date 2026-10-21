#include "Sound/Spu.h"

#include "Sound/SpuAdpcm.h"
#include "Sound/SpuCdInput.h"
#include "Sound/SpuEnvelope.h"
#include "Sound/SpuReverb.h"
#include "Sound/SpuVoice.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		constexpr uint32 VoiceRegisters = 0x180;
		constexpr uint32 RegisterCount = 0x200;

		enum ERegister : uint32
		{
			MainVolumeLeft = 0x180,
			MainVolumeRight = 0x182,
			ReverbVolumeLeft = 0x184,
			ReverbVolumeRight = 0x186,
			KeyOn = 0x188,
			KeyOff = 0x18C,
			PitchModulation = 0x190,
			NoiseEnable = 0x194,
			ReverbEnable = 0x198,
			EndFlags = 0x19C,
			ReverbStart = 0x1A2,
			TransferAddress = 0x1A6,
			TransferFifo = 0x1A8,
			Control = 0x1AA,
			Status = 0x1AE,
			CdVolumeLeft = 0x1B0,
			CdVolumeRight = 0x1B2,
			ReverbConfiguration = 0x1C0,
		};

		constexpr uint16 ControlUnmute = 0x4000;
		constexpr uint16 ControlReverbWrites = 0x0080;
		constexpr uint16 ControlCdReverb = 0x0004;
		constexpr uint16 ControlCdInput = 0x0001;
		constexpr uint16 ControlMode = 0x003F;

		int32 Clamp16(int32 Value)
		{
			return std::clamp(Value, -0x8000, 0x7FFF);
		}

		/** Sets or clears the bits of a 24-bit voice mask that one half-word register holds. */
		void SetHalf(uint32& Mask, uint32 Offset, uint16 Value)
		{
			const uint32 Shift = (Offset & 2) != 0 ? 16 : 0;
			Mask = (Mask & ~(0xFFFFu << Shift)) | (static_cast<uint32>(Value) << Shift);
			Mask &= 0xFFFFFF;
		}
	}

	struct FSpu::FState
	{
		std::array<uint8, RamSize> SoundRam{};
		std::array<uint16, RegisterCount> Registers{};
		std::array<FSpuVoice, VoiceCount> Voices{};
		FSpuVolume MainLeft;
		FSpuVolume MainRight;
		uint32 Ended = 0;
		uint32 Modulated = 0;
		uint32 Noisy = 0;
		uint32 Reverberant = 0;
		/** Where the next transferred byte goes, which the transfer address register sets. */
		uint32 TransferCursor = 0;
		FSpuNoise Noise;
		FSpuReverb Reverb;
		FSpuCdInput CdInput{CdInputFrames};
		std::array<int32, VoiceCount> VoiceGains = [] {
			std::array<int32, VoiceCount> Gains;
			Gains.fill(FullGain);
			return Gains;
		}();
		int32 CdGain = FullGain;
	};

	FSpu::FSpu()
		: State(std::make_unique<FState>())
	{
	}

	std::size_t FSpu::QueueCdInput(std::span<const int16> Stereo)
	{
		return State->CdInput.Queue(Stereo);
	}

	std::size_t FSpu::QueuedCdInput() const
	{
		return State->CdInput.QueuedFrames();
	}

	void FSpu::ClearCdInput()
	{
		State->CdInput.Clear();
	}

	void FSpu::SetVoiceGain(int32 Voice, int32 Gain)
	{
		if (Voice >= 0 && Voice < VoiceCount)
		{
			State->VoiceGains[static_cast<std::size_t>(Voice)] = std::clamp(Gain, 0, FullGain);
		}
	}

	void FSpu::SetCdGain(int32 Gain)
	{
		State->CdGain = std::clamp(Gain, 0, FullGain);
	}

	FSpu::~FSpu() = default;

	std::span<uint8> FSpu::Ram()
	{
		return State->SoundRam;
	}

	std::span<const uint8> FSpu::Ram() const
	{
		return State->SoundRam;
	}

	void FSpu::Write(uint32 Offset, uint16 Value)
	{
		FState& S = *State;
		Offset &= (RegisterCount * 2 - 1) & ~1u;
		S.Registers[Offset / 2] = Value;
		if (Offset < VoiceRegisters)
		{
			S.Voices[Offset / 16].Write(Offset % 16, Value);
			return;
		}
		switch (Offset)
		{
		case MainVolumeLeft:
			S.MainLeft.Write(Value);
			return;
		case MainVolumeRight:
			S.MainRight.Write(Value);
			return;
		case KeyOn:
		case KeyOn + 2:
		{
			const uint32 First = Offset == KeyOn ? 0 : 16;
			for (uint32 Bit = 0; Bit < 16 && First + Bit < VoiceCount; ++Bit)
			{
				if (Value & (1u << Bit))
				{
					S.Voices[First + Bit].KeyOn();
					S.Ended &= ~(1u << (First + Bit));
				}
			}
			return;
		}
		case KeyOff:
		case KeyOff + 2:
		{
			const uint32 First = Offset == KeyOff ? 0 : 16;
			for (uint32 Bit = 0; Bit < 16 && First + Bit < VoiceCount; ++Bit)
			{
				if (Value & (1u << Bit))
				{
					S.Voices[First + Bit].KeyOff();
				}
			}
			return;
		}
		case PitchModulation:
		case PitchModulation + 2:
			SetHalf(S.Modulated, Offset, Value);
			return;
		case NoiseEnable:
		case NoiseEnable + 2:
			SetHalf(S.Noisy, Offset, Value);
			return;
		case ReverbEnable:
		case ReverbEnable + 2:
			SetHalf(S.Reverberant, Offset, Value);
			return;
		case ReverbStart:
			S.Reverb.SetStart(Value);
			return;
		case TransferAddress:
			S.TransferCursor = static_cast<uint32>(Value) * 8;
			return;
		case TransferFifo:
			// Software fills the FIFO and then starts a manual transfer; the data lands the same.
			S.SoundRam[S.TransferCursor] = static_cast<uint8>(Value);
			S.SoundRam[S.TransferCursor + 1] = static_cast<uint8>(Value >> 8);
			S.TransferCursor = (S.TransferCursor + 2) & (RamSize - 1);
			return;
		default:
			if (Offset >= ReverbConfiguration)
			{
				S.Reverb.WriteRegister(static_cast<int32>((Offset - ReverbConfiguration) / 2), Value);
			}
			return;
		}
	}

	void FSpu::WriteDma(std::span<const uint8> Data)
	{
		FState& S = *State;
		for (const uint8 Byte : Data)
		{
			S.SoundRam[S.TransferCursor] = Byte;
			S.TransferCursor = (S.TransferCursor + 1) & (RamSize - 1);
		}
	}

	uint16 FSpu::Read(uint32 Offset) const
	{
		const FState& S = *State;
		Offset &= (RegisterCount * 2 - 1) & ~1u;
		if (Offset < VoiceRegisters)
		{
			const FSpuVoice& Voice = S.Voices[Offset / 16];
			switch (Offset % 16)
			{
			case 0x0C:
				return static_cast<uint16>(Voice.Level);
			case 0x0E:
				return Voice.Repeat;
			default:
				return S.Registers[Offset / 2];
			}
		}
		switch (Offset)
		{
		case EndFlags:
			return static_cast<uint16>(S.Ended);
		case EndFlags + 2:
			return static_cast<uint16>(S.Ended >> 16);
		case Status:
			return static_cast<uint16>(S.Registers[Control / 2] & ControlMode);
		default:
			return S.Registers[Offset / 2];
		}
	}

	void FSpu::Render(std::span<int16> Stereo)
	{
		FState& S = *State;
		const uint16 Attributes = S.Registers[Control / 2];
		const bool bUnmuted = (Attributes & ControlUnmute) != 0;
		const bool bReverbWrites = (Attributes & ControlReverbWrites) != 0;
		const int32 ReverbLeft = static_cast<int16>(S.Registers[ReverbVolumeLeft / 2]);
		const int32 ReverbRight = static_cast<int16>(S.Registers[ReverbVolumeRight / 2]);
		const bool bCdInput = (Attributes & ControlCdInput) != 0;
		const bool bCdReverb = (Attributes & ControlCdReverb) != 0;
		const int32 CdLeft = static_cast<int16>(S.Registers[CdVolumeLeft / 2]);
		const int32 CdRight = static_cast<int16>(S.Registers[CdVolumeRight / 2]);
		const uint16 NoiseClock = static_cast<uint16>((Attributes >> 8) & 0x3F);

		for (std::size_t Frame = 0; Frame + 1 < Stereo.size(); Frame += 2)
		{
			// The drive delivers a frame every sample, whether the SPU takes it in or not.
			const std::array<int32, 2> Cd = S.CdInput.Take();
			int32 Left = 0;
			int32 Right = 0;
			std::array<int32, 2> ToReverb{};
			int32 PreviousOutput = 0;
			for (int32 Index = 0; Index < VoiceCount; ++Index)
			{
				FSpuVoice& Voice = S.Voices[static_cast<std::size_t>(Index)];
				const uint32 Bit = 1u << Index;
				const bool bModulated = Index > 0 && (S.Modulated & Bit) != 0;
				const int32 Output = Voice.Sample(S.SoundRam, (S.Noisy & Bit) != 0 ? std::optional<int16>(S.Noise.Level()) : std::nullopt,
					bModulated ? std::optional<int32>(PreviousOutput) : std::nullopt, S.Ended, Bit);
				PreviousOutput = Output;
				const int32 Gain = S.VoiceGains[static_cast<std::size_t>(Index)];
				const int32 VoiceLeft = (Output * Voice.Left.Level >> 15) * Gain >> 15;
				const int32 VoiceRight = (Output * Voice.Right.Level >> 15) * Gain >> 15;
				Left += VoiceLeft;
				Right += VoiceRight;
				if (S.Reverberant & Bit)
				{
					ToReverb[0] += VoiceLeft;
					ToReverb[1] += VoiceRight;
				}
				Voice.Left.Tick();
				Voice.Right.Tick();
			}
			S.Noise.Tick(NoiseClock);

			// Muting silences the voices only; the CD input is heard regardless.
			if (!bUnmuted)
			{
				Left = 0;
				Right = 0;
				ToReverb = {};
			}
			if (bCdInput)
			{
				const int32 FromCdLeft = (Cd[0] * CdLeft >> 15) * S.CdGain >> 15;
				const int32 FromCdRight = (Cd[1] * CdRight >> 15) * S.CdGain >> 15;
				Left += FromCdLeft;
				Right += FromCdRight;
				if (bCdReverb)
				{
					ToReverb[0] += FromCdLeft;
					ToReverb[1] += FromCdRight;
				}
			}
			// The main volume applies to the whole mix, the reverb's output included.
			const std::array<int32, 2> Reverb = S.Reverb.Process({Clamp16(ToReverb[0]), Clamp16(ToReverb[1])}, S.SoundRam, bReverbWrites);
			Left += Reverb[0] * ReverbLeft >> 15;
			Right += Reverb[1] * ReverbRight >> 15;
			Stereo[Frame] = static_cast<int16>(Clamp16(Clamp16(Left) * S.MainLeft.Level >> 15));
			Stereo[Frame + 1] = static_cast<int16>(Clamp16(Clamp16(Right) * S.MainRight.Level >> 15));
			S.MainLeft.Tick();
			S.MainRight.Tick();
		}
	}
}
