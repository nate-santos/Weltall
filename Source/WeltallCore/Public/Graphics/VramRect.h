#pragma once

#include "Foundation/Types.h"

namespace Weltall
{
	/** A rectangle in the PlayStation's VRAM, measured in 16-bit units. */
	struct FVramRect
	{
		int16 X = 0;
		int16 Y = 0;
		int16 Width = 0;
		int16 Height = 0;

		bool operator==(const FVramRect&) const = default;
	};
}
