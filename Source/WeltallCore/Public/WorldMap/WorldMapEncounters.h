#pragma once

#include "Foundation/Types.h"
#include "Game/GameBalance.h"
#include "WorldMap/WorldMapTables.h"
#include "WorldMap/WorldMapTerrain.h"

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace Weltall
{
	class FPsxRandom;
	struct FWorldMapRegion;

	/**
	 * The world map's encounter schedule: a period of moving ticks (384, or 768 in the Yggdrasil) with
	 * one encounter at a random tick of each, drawn as the period starts.
	 */
	struct FWorldMapEncounters
	{
		static constexpr int32 SlotCapacity = 16;
		static constexpr int32 FootPeriod = 0x180;
		static constexpr int32 VehiclePeriod = 0x300;

		/** Each slot's ticks until its encounter. */
		std::array<int16, SlotCapacity> Timers{};
		/** Moving ticks left in the period. */
		int32 Left = 0;
		int32 Period = 0;
		/** Encounters a period: always 1. */
		int32 Slots = 0;
		/** Encounters that fell on the last moving tick; the loop's check reads and clears it. */
		int32 Fired = 0;
		/** How often the player asked them to come: a period lengthened or shortened to suit, or none counted. */
		EEncounterRate Rate = EEncounterRate::Original;

		/** The encounters reset: a new period on the next moving tick. */
		WELTALLCORE_API void Reset(bool bInVehicle);
		/** One moving tick, which none of the player's rate leaves uncounted; rand() is the game's. */
		WELTALLCORE_API void Tick(FPsxRandom& Random);
	};

	/**
	 * The formation picked: the battle for a place, from its cell's encounter region (forest cells
	 * use the program's forest regions) and the story's band: a formation drawn by its weight, or none
	 * where the band has no weights. rand() is drawn only when there are weights.
	 */
	WELTALLCORE_API std::optional<uint8> PickWorldMapFormation(const FWorldMapGround& Ground, std::span<const FWorldMapRegion> Regions,
		const FWorldMapVector& Place, uint16 StoryVariable, FPsxRandom& Random);

	/** The region a battle at a place draws from. */
	WELTALLCORE_API int32 WorldMapEncounterRegion(const FWorldMapGround& Ground, const FWorldMapVector& Place);

	/** What a place stands on among a trigger list's records: a place (its name), a notice (its message), or neither. */
	struct FWorldMapTriggerHit
	{
		int16 Place = -1;
		int16 Notice = -1;
		/** The place's record, which entering it reads, and where it is in its list; none for a notice or nothing. */
		std::optional<FWorldMapTrigger> Record;
		int32 RecordIndex = -1;

		bool operator==(const FWorldMapTriggerHit&) const = default;
	};

	/** The trigger: the first record of the list whose rectangle holds the place (whole units, edges included). */
	WELTALLCORE_API FWorldMapTriggerHit FindWorldMapTrigger(std::span<const FWorldMapTrigger> List, const FWorldMapVector& Place);
}
