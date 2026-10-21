#include "Formats/Wave.h"
#include "Sound/SoundDriverState.h"

#include <algorithm>
#include <cmath>

namespace Weltall
{
	void FSoundDriver::MixReplacementMusic(std::span<int16> Stereo, int32 Gain)
	{
		FState& S = *State;
		if (S.Flags & DriverFlag::Paused) return;
		for (int32 Number = 0; Number < S.SequenceCount; ++Number)
		{
			FSoundSequence& Sequence = *S.Sequences[static_cast<std::size_t>(Number)];
			if ((!Sequence.IsPlaying() && !Sequence.bWaveTail) || Sequence.File == nullptr) continue;
			if (Sequence.IsPlaying()) Sequence.PlaybackFrames += Stereo.size() / 2;
			if (!Sequence.File->Replacement) continue;
			const FWave& Wave = *Sequence.File->Replacement;
			if (Wave.Frames() == 0 || (Wave.Channels != 1 && Wave.Channels != 2)) continue;
			const double LoopFirst = Wave.Loop ? Wave.Loop->FirstFrame : 0;
			const double LoopEnd = Wave.Loop ? Wave.Loop->EndFrame : static_cast<double>(Wave.Frames());
			const double Step = static_cast<double>(Wave.SampleRate) / 44100.0
				* std::max(0.0, static_cast<double>(Sequence.Scale.Value) / 16777216.0)
				* std::exp2(static_cast<double>(Sequence.Pitch.Value) / (65536.0 * 256.0 * 12.0));
			const double Volume = std::clamp(static_cast<double>(Sequence.Volume.Value) / 2130706432.0, 0.0, 1.0)
				* std::clamp(static_cast<double>(S.MasterVolume) / 16383.0, 0.0, 1.0) * (static_cast<double>(Gain) / 32768.0);
			const double Pan = std::clamp(static_cast<double>(Sequence.Pan.Value) / (16777216.0 * 64.0), -1.0, 1.0);
			const bool bStereo = (S.Flags & DriverFlag::Stereo) != 0;
			const bool bAudible = Sequence.Muted == 0 && (Sequence.Flags & SequenceFlag::Seeking) == 0;
			for (std::size_t Frame = 0; Frame < Stereo.size() / 2; ++Frame)
			{
				if (Sequence.bWaveTail && Sequence.WaveFrame >= static_cast<double>(Wave.Frames()))
				{
					Sequence.bWaveTail = false;
					break;
				}
				if (Sequence.WaveFrame >= LoopEnd)
				{
					Sequence.WaveFrame = LoopFirst + std::fmod(Sequence.WaveFrame - LoopFirst, LoopEnd - LoopFirst);
				}
				if (bAudible)
				{
					const std::size_t At = static_cast<std::size_t>(Sequence.WaveFrame);
					const std::size_t Next = At + 1 >= static_cast<std::size_t>(LoopEnd)
						? (Sequence.bWaveTail ? At : static_cast<std::size_t>(LoopFirst)) : At + 1;
					const double Fraction = Sequence.WaveFrame - static_cast<double>(At);
					const auto Sample = [&](std::size_t Channel) {
						const std::size_t Side = Wave.Channels == 1 ? 0 : Channel;
						const int16 First = Wave.Samples[At * Wave.Channels + Side];
						return First + (Wave.Samples[Next * Wave.Channels + Side] - First) * Fraction;
					};
					double Left = Sample(0);
					double Right = Sample(1);
					if (!bStereo) Left = Right = (Left + Right) * 0.5;
					else
					{
						Left *= 1.0 - std::max(0.0, Pan);
						Right *= 1.0 + std::min(0.0, Pan);
					}
					const double LeftSign = (S.Flags & DriverFlag::InvertMainLeft) ? -1.0 : 1.0;
					const double RightSign = (S.Flags & DriverFlag::InvertMainRight) ? -1.0 : 1.0;
					Stereo[Frame * 2] = static_cast<int16>(std::clamp(std::lround(Stereo[Frame * 2] + Left * Volume * LeftSign), -32768l, 32767l));
					Stereo[Frame * 2 + 1] = static_cast<int16>(std::clamp(std::lround(Stereo[Frame * 2 + 1] + Right * Volume * RightSign), -32768l, 32767l));
				}
				Sequence.WaveFrame += Step;
			}
		}
	}
}
