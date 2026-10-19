#include "EventOps.h"

#include "Field/FieldWalkmesh.h"
#include "Game/PsxRandom.h"

namespace Weltall::EventOps
{
	namespace
	{
		constexpr uint16 PlayerLocked = 0xFFFF;
		constexpr uint32 PlayerCameraFlags = 0xC000;
		constexpr int32 MaxEncountersPerPeriod = FFieldEncounters::MaxPerPeriod;

		/** F7: the random encounters: a period in ticks and how many encounters it holds (at most 32). */
		void SetEncounterRate(FEventContext& Context)
		{
			FFieldEncounters& Encounters = Context.Encounters;
			Encounters.Period = ReadValue(Context, 1);
			Encounters.PerPeriod = ReadValue(Context, 3);
			if (Encounters.PerPeriod > MaxEncountersPerPeriod)
			{
				Encounters.PerPeriod = MaxEncountersPerPeriod;
			}
			if (Context.Random == nullptr || !Encounters.Schedule(*Context.Random, Context.EncounterRate))
			{
				Fail(Context, EEventFault::EncountersNeverScheduled, static_cast<uint32>(Encounters.PerPeriod));
				return;
			}
			Advance(Context, 5);
		}

		/**
		 * 72 and 75: a song for the map, silent for the scripts to fade in (72) or at full volume (75).
		 * The start is set even when the instruction then waits for a song still loading.
		 */
		template <bool bSilent>
		void LoadSong(FEventContext& Context)
		{
			if (Context.bBattleRequested)
			{
				Context.Run.bYield = true;
				return;
			}
			const int32 Song = ReadValue(Context, 1);
			if (Context.Songs == nullptr)
			{
				Fail(Context, EEventFault::NoSongs, static_cast<uint32>(Song));
				return;
			}
			Context.Songs->SetSongStart(Song, bSilent);
			if (!Context.bInitDone)
			{
				Context.Songs->ChooseSong(Song);
			}
			else
			{
				if (Context.Songs->IsLoading())
				{
					Context.Run.bYield = true;
					return;
				}
				if (!Context.Songs->ChangeSong(Song))
				{
					Fail(Context, EEventFault::NoSongs, static_cast<uint32>(Song));
					return;
				}
			}
			Advance(Context, 3);
		}

		/**
		 * 80: one byte lane (0-3) of a walkmesh attribute word, which opens or closes ground to walkers;
		 * a later lane changes nothing. The value is not masked: past a byte it spills into the lanes above.
		 */
		void SetWalkAttributeByte(FEventContext& Context)
		{
			const uint32 Value = static_cast<uint32>(ReadValue(Context, 3));
			const uint8 Index = CodeByte(Context, 1);
			const uint8 Lane = CodeByte(Context, 2);
			if (Lane < 4)
			{
				if (Context.Walkmesh == nullptr || Index >= Context.Walkmesh->Attributes.size())
				{
					Fail(Context, EEventFault::TableIndexOutOfRange, Index);
					return;
				}
				uint32& Word = Context.Walkmesh->Attributes[Index];
				const uint32 Shift = 8u * Lane;
				Word = (Word & ~(0xFFu << Shift)) | Value << Shift;
			}
			Advance(Context, 5);
		}

		/** 7F: the angle every pushing floor of the map turns its push by. */
		void SetFloorPushAngle(FEventContext& Context)
		{
			Context.Movement.FloorPushAngle = static_cast<uint16>(ReadValue(Context, 1));
			Advance(Context, 3);
		}

		/** FE 39: how fast every pushing floor of the map pushes, 4096 its own speed. */
		void SetFloorPushScale(FEventContext& Context)
		{
			Context.Movement.FloorPushScale = static_cast<int16>(ReadValue(Context, 1));
			Advance(Context, 3);
		}

		/** FE B8: how battles start, for random encounters (0) or for scripted battles. */
		void SetBattleKind(FEventContext& Context)
		{
			const uint8 Kind = static_cast<uint8>(ReadValue(Context, 2));
			if (CodeByte(Context, 1) == 0)
			{
				Context.EncounterBattleKind = Kind;
			}
			else
			{
				Context.ScriptedBattleKind = Kind;
			}
			Advance(Context, 4);
		}

		/** FE B7: whether the gears can be boarded or left here; 0xFF leaves it to the map's gear mode. */
		void SetGearModeOverride(FEventContext& Context)
		{
			Context.GearBoardingOverride = static_cast<uint16>(ReadValue(Context, 1));
			Advance(Context, 3);
		}

		/** FE 4F and FE 50: lets or keeps the player open the menu. */
		template <bool bLocked>
		void SetMenuLocked(FEventContext& Context)
		{
			Context.bMenuLocked = bLocked;
			Advance(Context, 1);
		}

		/** FE 99: whether the menu the player opens here offers saving: the operand 0 makes the map a save point. */
		void SetMenuSaving(FEventContext& Context)
		{
			Context.MenuParam = static_cast<uint8>(CodeByte(Context, 1) ^ 1);
			Advance(Context, 2);
		}

		/** FE 51 and FE 52: shows or hides the compass. */
		template <bool bHidden>
		void SetCompassHidden(FEventContext& Context)
		{
			Context.bCompassHidden = bHidden;
			Advance(Context, 1);
		}

		/** FE 53: the player controls the party again: the pad, the menu, the compass and turning the camera. */
		void EnablePlayer(FEventContext& Context)
		{
			Context.PlayerLock = 0;
			Context.bMenuLocked = false;
			Context.bCompassHidden = false;
			Context.Camera.Flags &= ~PlayerCameraFlags;
			Advance(Context, 1);
		}

		/** FE 54: takes the party from the player, waiting while a battle or the world map is on its way. */
		void DisablePlayer(FEventContext& Context)
		{
			Context.PlayerLock = PlayerLocked;
			Context.bMenuLocked = true;
			Context.bCompassHidden = true;
			Context.Camera.Flags |= PlayerCameraFlags;
			if (Context.bBattleRequested || Context.Requests.bWorldMap)
			{
				Context.Run.bYield = true;
				Advance(Context, -1);
				return;
			}
			Advance(Context, 1);
		}
	}

	void RegisterFieldState(FBaseTable& Base, FExtendedTable& Extended)
	{
		Base[0x72] = &LoadSong<true>;
		Base[0x75] = &LoadSong<false>;
		Base[0x7F] = &SetFloorPushAngle;
		Base[0x80] = &SetWalkAttributeByte;
		Base[0xF7] = &SetEncounterRate;
		Extended[0x39] = &SetFloorPushScale;
		Extended[0x4F] = &SetMenuLocked<false>;
		Extended[0x50] = &SetMenuLocked<true>;
		Extended[0x51] = &SetCompassHidden<false>;
		Extended[0x52] = &SetCompassHidden<true>;
		Extended[0x53] = &EnablePlayer;
		Extended[0x54] = &DisablePlayer;
		Extended[0xB7] = &SetGearModeOverride;
		Extended[0xB8] = &SetBattleKind;
		Extended[0x99] = &SetMenuSaving;
	}
}
