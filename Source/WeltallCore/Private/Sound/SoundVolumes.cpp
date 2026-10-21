#include "Sound/SoundVolumes.h"

#include "Sound/Spu.h"

#include <algorithm>

namespace Weltall
{
	int32 SoundVolumeGain(int32 Percent)
	{
		const int32 Clamped = std::clamp(Percent, 0, FSoundVolumes::Full);
		return Clamped * Clamped * FSpu::FullGain / (FSoundVolumes::Full * FSoundVolumes::Full);
	}
}
