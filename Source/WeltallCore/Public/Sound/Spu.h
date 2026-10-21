#pragma once

#include "Foundation/Types.h"
#include "Sound/SpuBus.h"

#include <array>
#include <memory>
#include <span>

namespace Weltall
{
	/**
	 * The PlayStation's sound processor: 24 voices playing ADPCM samples from its 512 KB of sound
	 * RAM through their envelopes and volumes, with noise, pitch modulation and the reverb unit,
	 * mixed with the CD drive's sound to 16-bit stereo at 44,100 Hz. Software drives it through its
	 * registers, as a game's sound code does, and puts sample data into its RAM through the transfer
	 * registers or DMA.
	 */
	class FSpu final : public ISpuBus
	{
	public:
		static constexpr uint32 RamSize = 0x80000;
		static constexpr int32 VoiceCount = 24;
		static constexpr int32 SampleRate = 44100;

		WELTALLCORE_API FSpu();
		WELTALLCORE_API ~FSpu() override;
		FSpu(const FSpu&) = delete;
		FSpu& operator=(const FSpu&) = delete;

		/** A register write, at an offset from 1F801C00 (0 to 0x3FE); it takes effect at the next sample. */
		WELTALLCORE_API void Write(uint32 Offset, uint16 Value) override;

		/** What a register reads: the value written, or the voice's envelope level, the end flags and the like. */
		WELTALLCORE_API uint16 Read(uint32 Offset) const override;

		WELTALLCORE_API void WriteDma(std::span<const uint8> Data) override;

		/** Sound RAM, where software transfers sample data and the reverb keeps its work area. */
		WELTALLCORE_API std::span<uint8> Ram();
		WELTALLCORE_API std::span<const uint8> Ram() const;

		/** Produces the next samples, left and right interleaved: Stereo.size() / 2 of them. */
		WELTALLCORE_API void Render(std::span<int16> Stereo);

		/** The most frames the CD input holds: two seconds. */
		static constexpr std::size_t CdInputFrames = SampleRate * 2;

		/**
		 * Sound from the CD drive (its decoded XA or CD audio) as it reaches the SPU's CD input:
		 * stereo frames at 44,100 Hz, one taken at every sample from the next one on, silence when
		 * none is left. The CD volumes and the control register's CD bits decide how it is heard.
		 * Returns how many frames fitted.
		 */
		WELTALLCORE_API std::size_t QueueCdInput(std::span<const int16> Stereo);
		/** The frames the CD input still holds. */
		WELTALLCORE_API std::size_t QueuedCdInput() const;
		/** Drops what the CD input holds, as when the drive stops. */
		WELTALLCORE_API void ClearCdInput();

		/** The player's volume as gains: 8000h hears a voice or the CD input as made, 0 silences it. */
		static constexpr int32 FullGain = 0x8000;
		/**
		 * The player's volume for a voice, applied as its sound joins the mix and the reverb's input; the next voice's
		 * pitch modulation still takes its sound as made. The console has no such control: no register reaches it.
		 */
		WELTALLCORE_API void SetVoiceGain(int32 Voice, int32 Gain);
		/** The player's volume for the CD input (the movies' sound), as SetVoiceGain's. */
		WELTALLCORE_API void SetCdGain(int32 Gain);

	private:
		struct FState;
		std::unique_ptr<FState> State;
	};
}
