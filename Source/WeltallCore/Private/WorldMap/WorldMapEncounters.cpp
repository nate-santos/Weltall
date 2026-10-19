#include "WorldMap/WorldMapEncounters.h"

#include "Game/PsxRandom.h"
#include "WorldMap/WorldMapScene.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		constexpr int32 ForestType = 4;
		constexpr int16 NoticeKind = 4;
		constexpr int32 MostTicks = 0x7FFF;
	}

	void FWorldMapEncounters::Reset(bool bInVehicle)
	{
		Timers.fill(0);
		Left = 1;
		Period = bInVehicle ? VehiclePeriod : FootPeriod;
		Slots = 1;
		Fired = 0;
	}

	void FWorldMapEncounters::Tick(FPsxRandom& Random)
	{
		if (Rate == EEncounterRate::None)
		{
			Fired = 0;
			return;
		}
		--Left;
		if (Left == 0)
		{
			// The player's rate stretches or shrinks the period as it starts; the timers count to it in 16 bits.
			const int32 Ticks = std::min(EncounterPeriodAt(Period, Rate), MostTicks);
			// Each slot's tick is drawn again until it differs from the slots' before it.
			for (int32 Slot = 0; Slot < Slots; ++Slot)
			{
				int32 Draw = 0;
				bool bTaken = true;
				while (bTaken)
				{
					Draw = Random.Next() % Ticks + 1;
					bTaken = false;
					for (int32 Earlier = 0; Earlier < Slot; ++Earlier)
					{
						bTaken = bTaken || Timers[static_cast<std::size_t>(Earlier)] == Draw;
					}
				}
				Timers[static_cast<std::size_t>(Slot)] = static_cast<int16>(Draw);
			}
			Left = Ticks;
		}
		Fired = 0;
		for (int32 Slot = 0; Slot < Slots; ++Slot)
		{
			int16& Timer = Timers[static_cast<std::size_t>(Slot)];
			if (Timer == 1)
			{
				++Fired;
			}
			Timer = static_cast<int16>(Timer - 1);
		}
	}

	int32 WorldMapEncounterRegion(const FWorldMapGround& Ground, const FWorldMapVector& Place)
	{
		const int32 Region = Ground.Region(Place.X, Place.Z);
		if (Ground.TerrainType(Place.X, Place.Z) == ForestType)
		{
			return Ground.Tables().ForestRegions[static_cast<std::size_t>(Region)];
		}
		return Region;
	}

	std::optional<uint8> PickWorldMapFormation(const FWorldMapGround& Ground, std::span<const FWorldMapRegion> Regions,
		const FWorldMapVector& Place, uint16 StoryVariable, FPsxRandom& Random)
	{
		const FWorldMapRegion& Region = Regions[static_cast<std::size_t>(WorldMapEncounterRegion(Ground, Place))];
		std::size_t Band = 0;
		for (const uint16 Threshold : Ground.Tables().BandThresholds)
		{
			if (Threshold > StoryVariable)
			{
				break;
			}
			++Band;
		}
		std::array<uint8, FWorldMapRegion::FormationCount> Weights = Region.Weights[Band];
		int32 Total = 0;
		for (const uint8 Weight : Weights)
		{
			Total += Weight;
		}
		if (Total <= 0)
		{
			return std::nullopt;
		}
		int32 Remaining = Random.Next() % Total + 1;
		std::size_t Formation = 0;
		do
		{
			--Remaining;
			while (Weights[Formation] == 0)
			{
				++Formation;
			}
			--Weights[Formation];
		} while (Remaining > 0);
		return static_cast<uint8>(Formation);
	}

	FWorldMapTriggerHit FindWorldMapTrigger(std::span<const FWorldMapTrigger> List, const FWorldMapVector& Place)
	{
		const int32 X = static_cast<int32>(static_cast<uint32>(Place.X) >> 12 & 0xFFFF);
		const int32 Z = static_cast<int32>(static_cast<uint32>(Place.Z) >> 12 & 0xFFFF);
		FWorldMapTriggerHit Hit;
		for (std::size_t Index = 0; Index < List.size(); ++Index)
		{
			const FWorldMapTrigger& Record = List[Index];
			if (Record.X <= X && X <= Record.X + Record.Width && Record.Z <= Z && Z <= Record.Z + Record.Height)
			{
				if (Record.Kind == NoticeKind)
				{
					Hit.Notice = Record.Name;
					return Hit;
				}
				Hit.Place = Record.Name;
				Hit.Record = Record;
				Hit.RecordIndex = static_cast<int32>(Index);
				return Hit;
			}
		}
		return Hit;
	}
}
