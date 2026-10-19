#include "Game/CharacterEquipment.h"
#include "Game/GameState.h"
#include "Game/GameStateLayout.h"
#include "Menu/MenuMain.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		constexpr uint8 Billy = 4;
		constexpr int32 LastSlot = 3;
		constexpr int32 ListRows = 8;
		/** The list's window and the scroll bar's knob on its left edge. */
		constexpr int32 KnobX = 0x94;
		constexpr int32 KnobY = 0x12;
		constexpr int32 KnobFull = 100;
		constexpr int32 KnobScrolling = 0x32;
		constexpr int32 EquipCursorKind = 3;
		constexpr int32 PiecesEntry = 4;
	}

	TMenuTask<bool> FMenuMain::EquipScreen(int32 Slot)
	{
		co_await Equip(Slot, true, false);
		co_return true;
	}

	void FMenuMain::OpenEquip(bool bGear)
	{
		const FMenuMainTables& Tables = Resources.Main();
		const std::size_t First = bGear ? 0x16 : 0x10;
		RenderLabels(EquipSlotLabels, std::span<const uint8>(Tables.LabelSets).subspan(First, EquipSlotLabels.size()), 4);
		InitTargetPointers(3);
		AllocCursor(0);
		DrawMemberHints(1, bGear);
	}

	TMenuTask<> FMenuMain::CloseEquip()
	{
		Flags[MenuFlag::EquipList] = 0;
		for (const int32 Window : {2, 3, 4, 5})
		{
			CloseWindow(Window);
		}
		co_await Frame();
		ClearFlags(MenuFlag::EquipSlotLabels, static_cast<int32>(EquipSlotLabels.size()));
		Flags[MenuFlag::ScrollKnob] = 0;
	}

	TMenuTask<> FMenuMain::Equip(int32 Slot, bool bFromArc, bool bGear)
	{
		bool bGoesOn = true;
		bool bNamesStale = true;
		int32 Page = 0;
		int32 DrawnSlot = 0xFF;
		int32 Which = 0;
		bool bFirst = true;
		int32 Row = 0;
		int32 DrawnWhich = 0xFF;
		int32 DrawnRow = 0xFF;
		bool bPreviews = false;
		bool bCursorShown = false;
		bool bConfirmNext = false;
		bool bListOpen = false;
		int32 DrawnTop = 0xFF;
		int32 Top = 0;
		int32 LastTop = 0;
		Flags[MenuFlag::StatusSheet] = 0;
		OpenEquip(bGear);
		SaveEquip(Slot, bGear);
		while (true)
		{
			co_await Frame();
			if (bNamesStale || Slot != DrawnSlot)
			{
				DrawEquipNames(Slot, Page + 1, bListOpen, bGear);
				DrawEquipSlots(Page);
				bNamesStale = false;
			}
			if (Top != DrawnTop || Slot != DrawnSlot)
			{
				LastTop = BuildEquipList(Slot, Top, Which, Page, bGear);
				const int32 KnobTop = LastTop == 0 ? 0 : Top * 100 / LastTop / 2;
				DrawScrollKnob(KnobX, KnobY + KnobTop, LastTop == 0 ? KnobFull : KnobScrolling);
			}
			if (bCursorShown)
			{
				DrawCursor(Row, Top, EquipCursorKind, 0);
			}
			else
			{
				Flags[MenuFlag::Cursors] = 0;
			}
			if (Row != DrawnRow || Slot != DrawnSlot || Top != DrawnTop)
			{
				if (bPreviews)
				{
					PreviewEquip(Slot, Which, Row, Top, Page, bGear);
				}
				bPreviews = true;
				if (!bGear)
				{
					RecomputeCharacterEquipment(*Services.Game, Member(Slot), Resources.Equipment());
					const FCharacterStats Stats = ComputeCharacterStats(*Services.Game, Member(Slot), !Services.Options.bAgilityShownCapped, UsesPerfectWorksRules());
					std::copy(Stats.Values.begin(), Stats.Values.end(), ShownStats.begin());
				}
				else
				{
					RecomputeGear(Slot);
				}
				DrawEquipDescription(Which, Row, Top, Page, bGear, false, Slot);
				DrawStats(true, bListOpen, bGear);
				DrawnRow = Row;
				DrawnTop = Top;
			}
			if (Which != DrawnWhich || Slot != DrawnSlot)
			{
				DrawEquipDescription(Which, Row, Top, Page, bGear, true, Slot);
				PlaceEquipPointer(Page, Which);
				DrawnWhich = Which;
				DrawnSlot = Slot;
			}
			if (bFirst)
			{
				OpenWindow(2, 0x94, 10, 0x94, 0x74, false, true, PiecesEntry, true);
				OpenWindow(3, 0x6C, 0x87, 0xC4, 0x48, false, true, PiecesEntry, false);
				OpenWindow(5, 8, 0x8E, 0x60, 0x40, false, true, PiecesEntry, false);
				if (bFromArc)
				{
					OpenLayer();
					co_await SlideParty(false, false);
					while (LayerState != EMenuLayer::Still)
					{
						co_await Frame();
					}
				}
				TargetPointerShown[0] = true;
				Flags[MenuFlag::PlayTime] = 0;
				bFirst = false;
				Flags[MenuFlag::Windows + 1] = 0;
			}
			Flags[MenuFlag::TargetPointers] = 1;
			if (bConfirmNext)
			{
				bConfirmNext = false;
				Code = EMenuCode::Confirm;
			}
			if (!bListOpen)
			{
				switch (Code)
				{
				case EMenuCode::Right:
				case EMenuCode::Left:
					if (Member(Slot) == Billy)
					{
						Page ^= 1;
						DrawnWhich = 0xFF;
						bNamesStale = true;
						Which = 0;
					}
					break;
				case EMenuCode::Down:
					Which = Which + 1 > LastSlot ? 0 : Which + 1;
					// A gear's second page has only its first and last slots.
					if (Page != 0 && bGear && Which >= 1 && Which <= 2)
					{
						Which = 3;
					}
					break;
				case EMenuCode::Up:
					Which = Which - 1 < 0 ? LastSlot : Which - 1;
					if (Page != 0 && bGear && Which >= 1 && Which <= 2)
					{
						Which = 0;
					}
					break;
				case EMenuCode::Confirm:
					SaveEquip(Slot, bGear);
					Top = 0;
					bListOpen = true;
					DrawnTop = 0xFF;
					DrawnRow = 0xFF;
					bNamesStale = true;
					TargetPointerShown[0] = false;
					bCursorShown = true;
					Row = 0;
					Flags[MenuFlag::Cursors] = 1;
					break;
				case EMenuCode::Cancel:
					bGoesOn = false;
					break;
				case EMenuCode::R1:
				case EMenuCode::L1:
				{
					const bool bPrevious = Code == EMenuCode::L1;
					if (NextMember(Slot, bPrevious, bGear) != Slot)
					{
						Slot = NextMember(Slot, bPrevious, bGear);
						Page = 0;
						bPreviews = false;
					}
					break;
				}
				default:
					break;
				}
			}
			else
			{
				bool bCloses = false;
				switch (Code)
				{
				case EMenuCode::Up:
					if (--Row < 0)
					{
						Row = 0;
						if (--Top < 0)
						{
							Top = 0;
						}
					}
					break;
				case EMenuCode::Down:
					if (++Row > ListRows - 1)
					{
						Row = ListRows - 1;
						if (++Top > LastTop)
						{
							Top = LastTop;
						}
					}
					break;
				case EMenuCode::Confirm:
					DrawnSlot = 0xFF;
					if (CommitEquip(Slot, Which, Page, bGear))
					{
						Page ^= 1;
						bNamesStale = true;
						bConfirmNext = true;
						ReturnBillyAmmo(Slot, bGear);
					}
					SaveEquip(Slot, bGear);
					bCursorShown = false;
					TargetPointerShown[0] = true;
					DrawCursor(0, Top, EquipCursorKind, 0);
					Row = 0;
					Flags[MenuFlag::Cursors] = 0;
					bCloses = true;
					break;
				case EMenuCode::Cancel:
					RevertEquip(Slot, bGear);
					bCursorShown = false;
					TargetPointerShown[0] = true;
					DrawCursor(0, Top, EquipCursorKind, 0);
					Row = 0;
					DrawnSlot = 0xFF;
					Flags[MenuFlag::Cursors] = 0;
					bCloses = true;
					break;
				default:
					break;
				}
				if (bCloses)
				{
					Row = 0;
					Top = 0;
					bListOpen = false;
					DrawnRow = 0xFF;
					bNamesStale = true;
					bPreviews = false;
					StatBars.bChangesShown = false;
				}
			}
			if (!bGoesOn)
			{
				Flags[MenuFlag::TargetPointers] = 0;
				Flags[MenuFlag::Cursors] = 0;
				co_return;
			}
		}
	}

	void FMenuMain::PlaceEquipPointer(int32 Page, int32 Which)
	{
		// The pointer beside the slot: half the red pointer's size, placed outright.
		if (TargetPointers[0].empty())
		{
			return;
		}
		constexpr int16 Left = 0x8C;
		constexpr int16 Right = 0x9C;
		constexpr int16 Height = 0x10;
		const int16 Top = static_cast<int16>(Resources.Main().EquipPointerY[static_cast<std::size_t>(Page * 4 + Which)]);
		FMenuPrimitive& Shown = TargetPointers[0].front();
		Shown.Corners[0].X = Left;
		Shown.Corners[0].Y = Top;
		Shown.Corners[1].X = Right;
		Shown.Corners[1].Y = Top;
		Shown.Corners[2].X = Left;
		Shown.Corners[2].Y = static_cast<int16>(Top + Height);
		Shown.Corners[3].X = Right;
		Shown.Corners[3].Y = static_cast<int16>(Top + Height);
	}

	uint8& FMenuMain::RecordByte(int32 Slot, bool bGear, std::size_t Offset)
	{
		const uint8 Character = Member(Slot);
		std::span<uint8> Block = Services.Game->Block();
		if (!bGear)
		{
			return Block[GameStateLayout::CharacterOffset(Character) + Offset];
		}
		const uint8 Gear = Block[GameStateLayout::CharacterOffset(Character) + GameStateLayout::Character::Gear];
		return Block[GameStateLayout::GearOffset(Gear) + Offset];
	}
}
