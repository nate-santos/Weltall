#include "Game/GameBalance.h"

#include <algorithm>
#include <limits>

namespace Weltall
{
	uint32 RewardAt(uint32 Amount, ERewardRate Rate)
	{
		uint64 Halves = 2;
		switch (Rate)
		{
		case ERewardRate::Original:
			return Amount;
		case ERewardRate::OneAndAHalf:
			Halves = 3;
			break;
		case ERewardRate::Double:
			Halves = 4;
			break;
		case ERewardRate::Triple:
			Halves = 6;
			break;
		case ERewardRate::Quadruple:
			Halves = 8;
			break;
		}
		return static_cast<uint32>(std::min<uint64>(uint64{Amount} * Halves / 2, std::numeric_limits<uint32>::max()));
	}

	int32 EncounterPeriodAt(int32 Period, EEncounterRate Rate)
	{
		switch (Rate)
		{
		case EEncounterRate::Quarter:
			return Period * 4;
		case EEncounterRate::Half:
			return Period * 2;
		case EEncounterRate::Double:
			return std::max(Period / 2, 1);
		case EEncounterRate::None:
		case EEncounterRate::Original:
			break;
		}
		return Period;
	}
}
