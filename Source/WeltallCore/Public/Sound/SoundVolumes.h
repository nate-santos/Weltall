#pragma once

#include "Foundation/Types.h"

namespace Weltall
{
	/** The player's volumes, each a percentage of the game's own: all its sound, then each kind of it. */
	struct FSoundVolumes
	{
		static constexpr int32 Full = 100;

		int32 Master = Full;
		int32 Music = Full;
		int32 Effects = Full;
		/** The recorded voices: the battles' shouts, cries and spoken lines. */
		int32 Voices = Full;
		/** The movies' sound, one recording of their speech, music and effects together. */
		int32 Movies = Full;

		bool operator==(const FSoundVolumes&) const = default;
	};

	/**
	 * A volume as the gain the mix applies, 8000h at 100 and 0 at 0: the percentage squared, so that each step down
	 * sounds about as much quieter as the last.
	 */
	WELTALLCORE_API int32 SoundVolumeGain(int32 Percent);
}
