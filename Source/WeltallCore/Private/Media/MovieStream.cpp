#include "Media/MovieStream.h"

#include <string>

namespace Weltall
{
	FMovieStream::FMovieStream(const FCdImage& InImage, uint32 FirstLba, uint32 SectorCount)
		: Image(InImage), NextLba(FirstLba), EndLba(FirstLba + SectorCount)
	{
	}

	TResult<std::optional<FStrFrame>> FMovieStream::NextFrame(std::vector<int16>& Sound)
	{
		while (NextLba < EndLba)
		{
			const uint32 Lba = NextLba++;
			const TResult<FCdSectorHeader> Header = Image.ReadHeader(Lba);
			if (!Header)
			{
				return Header.Error();
			}
			// Every Form 1 sector may be video, which its own header tells: the releases' are marked video, a fan
			// translation's re-encoded movies' data, and a cooked image keeps no subheaders. A cooked image's sound
			// sectors keep too little of their sound to be played.
			const bool bCooked = !Image.IsRaw();
			const bool bAudio = !bCooked && (Header->Submode & CdSector::SubmodeAudio) != 0 && Header->IsForm2();
			const bool bVideo = bCooked || !Header->IsForm2();
			if (!bAudio && !bVideo)
			{
				continue;
			}
			const TResult<std::vector<uint8>> UserData = Image.ReadUserData(Lba);
			if (!UserData)
			{
				return UserData.Error();
			}
			if (bAudio)
			{
				const FXaCodingInfo Coding = FXaCodingInfo::FromSubheader(Header->CodingInfo);
				if (Coding.bHalfRate)
				{
					return MakeError("sector " + std::to_string(Lba) + " holds 18900 Hz sound, which the resampler does not take");
				}
				Decoded.clear();
				DecodeXaSector(*UserData, Coding, History, Decoded);
				Resampler.Resample(Decoded, Coding.bStereo ? 2 : 1, Sound);
				continue;
			}
			if (std::optional<FStrFrame> Frame = Frames.Add(*UserData))
			{
				return std::optional<FStrFrame>(std::move(*Frame));
			}
		}
		return std::optional<FStrFrame>();
	}
}
