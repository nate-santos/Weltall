#include "Foundation/LittleEndian.h"
#include "Game/GameState.h"
#include "Game/ItemUse.h"
#include "Menu/MenuMain.h"

namespace Weltall
{
	namespace
	{
		constexpr int32 ItemsList = 0;
		constexpr int32 TargetWindow = 2;
		constexpr int32 PiecesEntry = 4;
		/** The list's labels drawn as the unusable are: left as they are when the list is dimmed. */
		constexpr uint8 UnusableGrey = 0x20;

		namespace Effect
		{
			constexpr uint8 Buzzer = 4;
			constexpr uint8 Used = 0x37;
		}
	}

	void FMenuMain::DimItemList(bool bDim)
	{
		const int32 Mode = bDim ? 1 : 0;
		DimWindow(3, bDim);
		DimWindow(4, bDim);
		ShadePiece(ScrollKnob.Quad, Mode);
		for (std::size_t Row = 0; Row < ItemList.Names.size(); ++Row)
		{
			if (ItemList.Names[Row].Quad.Corners[0].Color.R != UnusableGrey)
			{
				ShadePiece(ItemList.Names[Row].Quad, Mode);
				ShadePiece(ItemList.Counts[Row].Quad, Mode);
			}
		}
		for (FMenuLayerSprite& Cursor : Cursors)
		{
			ShadePiece(Cursor.Quad, Mode);
		}
		ShadePiece(ItemList.HelpName.Quad, Mode);
		ShadePiece(ItemList.HelpCount.Quad, Mode);
		ShadePiece(ItemList.Description.Quad, Mode);
		for (FMenuLabel& Tag : HelpTags)
		{
			ShadePiece(Tag.Quad, Mode);
		}
	}

	TMenuTask<bool> FMenuMain::UseItem(int32 Top, int32 Cursor)
	{
		const std::size_t Slot = static_cast<std::size_t>(Top * 2 + Cursor);
		const std::span<uint8> Items = Services.Game->InventoryItems(ItemsList);
		const std::span<uint8> Counts = Services.Game->InventoryCounts(ItemsList);
		const std::span<const uint8> Record = ItemRecordOf(Items[Slot]);
		const uint8 Use = Record.empty() ? 0 : Record[ItemRecord::Use];
		const bool bUsable = (Use & ItemRecord::InMenu) != 0 && ((Use & ItemRecord::AtSavePoints) == 0 || Request.Param != 0);
		if (!bUsable)
		{
			co_return false;
		}
		const uint16 Targeting = LittleEndian::ReadU16(Record, ItemRecord::Targeting);
		int32 Target = FirstMember;
		uint32 Targets = 0;
		bool bRedraws = true;
		bool bGoesOn = true;
		OpenWindow(TargetWindow, 0x10, 0x0E, 0x90, 0xB0, false, false, PiecesEntry, false);
		while (true)
		{
			co_await Frame();
			Targets = 0;
			if (bRedraws)
			{
				bRedraws = false;
				DrawItemPage(Top);
				DrawItemHelp(Cursor, Top);
				DimItemList(true);
				DrawTargets(0);
			}
			for (std::size_t Shown = 0; Shown < 3; ++Shown)
			{
				TargetPointerShown[Shown] = false;
			}
			// One member, or the whole party for an item that works on all.
			if ((Targeting & ItemRecord::WholeParty) == 0)
			{
				Targets = 1u << Target;
				TargetPointerShown[static_cast<std::size_t>(Target)] = true;
			}
			else
			{
				for (int32 Member = 0; Member < 3; ++Member)
				{
					if (this->Member(Member) != FGameState::EmptySlot)
					{
						Targets |= 1u << Member;
						TargetPointerShown[static_cast<std::size_t>(Member)] = true;
					}
				}
			}
			Flags[MenuFlag::TargetPointers] = 1;
			if (Counts[Slot] == 0)
			{
				bGoesOn = false;
			}
			if (!bGoesOn)
			{
				break;
			}
			switch (Code)
			{
			case EMenuCode::Up:
				Target = NextMember(Target, true, false);
				break;
			case EMenuCode::Down:
				Target = NextMember(Target, false, false);
				break;
			case EMenuCode::Confirm:
			{
				// Used on every target; spent once if it helped any.
				bool bHelped = false;
				for (int32 Member = 0; Member < 3; ++Member)
				{
					if ((Targets >> Member & 1) != 0 && ApplyItemInMenu(*Services.Game, this->Member(Member), ItemRecordOf(Items[Slot]), UsesPerfectWorksRules()))
					{
						bHelped = true;
					}
				}
				if (bHelped)
				{
					PlaySound(Effect::Used);
					if (--Counts[Slot] == 0)
					{
						Items[Slot] = 0;
					}
				}
				else
				{
					PlaySound(Effect::Buzzer);
				}
				bRedraws = true;
				break;
			}
			case EMenuCode::Cancel:
				bGoesOn = false;
				Targets = 0;
				break;
			default:
				break;
			}
			if (!bGoesOn)
			{
				break;
			}
		}
		for (std::size_t Shown = 0; Shown < 3; ++Shown)
		{
			TargetPointerShown[Shown] = false;
		}
		DimItemList(false);
		Flags[MenuFlag::TargetPanels] = 0;
		co_await Frame();
		bTargetPanelsMade = false;
		CloseWindow(TargetWindow);
		co_return Targets != 0;
	}
}
