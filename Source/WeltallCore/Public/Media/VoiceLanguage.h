#pragma once

#include "Foundation/Types.h"

namespace Weltall
{
	/** The voice track used by new movies and battles; text follows the gameplay disc. */
	enum class EVoiceLanguage : uint8
	{
		Automatic,
		English,
		Japanese,
	};

	/** When movies show the supported undub's subtitles, which only its discs carry. */
	enum class EMovieSubtitles : uint8
	{
		/** With the undub's own sound, as the undub shows them. */
		Automatic,
		/** Also over the USA's movies, whose pictures are the same, when the undub disc of that number is supplied. */
		On,
		Off,
	};
}
