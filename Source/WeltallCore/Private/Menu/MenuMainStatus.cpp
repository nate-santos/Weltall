#include "Game/CharacterEquipment.h"
#include "Game/GameState.h"
#include "Menu/MenuMain.h"
#include "Menu/MenuResources.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		/** The Status screen's inner arc from the bottom: Equip, Abilities, Skills; it starts on Skills. */
		constexpr int32 FirstSubCommand = 2;
		constexpr int32 NeverDrawn = 0xF3;
		/** The sheet's text labels: the inner arc's three, then the headings from the fourth on. */
		constexpr int32 FirstHeading = 3;
		constexpr int32 SheetLabelsBase = 4;
		constexpr int16 LabelRows = 13;
		/** A label beside an inner arc's orb: right of the orb's place and above it. */
		constexpr int32 SubLabelX = 0x16;
		constexpr int32 SubLabelY = -0x22;
		constexpr int32 InnerArc = 7;
		/** The L1 and R1 hints. */
		constexpr int32 PreviousHint = 0x164;
		constexpr int32 NextHint = 0x165;
		constexpr std::size_t HintParts = 2;
		constexpr int32 FullScale = 0x1000;
		/** The Normal Menu's commands whose screens close with the menu's layer. */
		constexpr int32 StatusCommand = 6;
		constexpr int32 EquipArcCommand = 5;
		constexpr int32 ItemsCommand = 4;
		constexpr int32 AbilitiesArcCommand = 3;
		constexpr int32 GearArcCommand = 2;
		constexpr int32 FileArcCommand = 1;
		constexpr int32 ContinueCommand = 8;
		/** The Status screen's inner commands: Equip, Abilities (then Skills). */
		constexpr int32 EquipCommand = 0;
		constexpr int32 AbilitiesCommand = 1;
	}

	TMenuTask<bool> FMenuMain::StatusScreen()
	{
		int32 Slot = FirstMember;
		int32 DrawnSlot = NeverDrawn;
		bool bFirst = true;
		bool bGoesOn = true;
		SubCommand = FirstSubCommand;
		DrawnSubCommand = 0xFF;
		OpenStatus();
		while (true)
		{
			co_await Frame();
			if (Slot != DrawnSlot)
			{
				const uint8 Character = Member(Slot);
				RecomputeCharacterEquipment(*Services.Game, Character, Resources.Equipment());
				const FCharacterStats Stats = ComputeCharacterStats(*Services.Game, Character, !Services.Options.bAgilityShownCapped, UsesPerfectWorksRules());
				std::copy(Stats.Values.begin(), Stats.Values.end(), ShownStats.begin());
				co_await DrawStatusSheet(Slot, false);
				DrawnSlot = Slot;
				if (bFirst)
				{
					bFirst = false;
					OpenLayer();
					co_await SlideParty(false, false);
					co_await RevealSubCommands(0);
				}
			}
			if (SubCommand != DrawnSubCommand)
			{
				PlaceSheetSubLabel();
				DrawSubCommands(0);
				DrawnSubCommand = SubCommand;
			}
			switch (Code)
			{
			case EMenuCode::Down:
				SubCommand = SubCommand == 0 ? SubCommandCount - 1 : SubCommand - 1;
				break;
			case EMenuCode::Up:
				SubCommand = SubCommand + 1 >= SubCommandCount ? 0 : SubCommand + 1;
				break;
			case EMenuCode::Confirm:
				HideSelection();
				ClearFlags(MenuFlag::SheetLabels, static_cast<int32>(SheetLabels.size()));
				Flags[MenuFlag::StatusSheet] = 0;
				Flags[MenuFlag::Stats] = 0;
				Flags[MenuFlag::EquipNames] = 0;
				if (SubCommand == EquipCommand)
				{
					Flags[MenuFlag::SubCommands] = 0;
					co_await Equip(Slot, false, false);
					co_await CloseEquip();
					DrawStats(false, false, false);
					DrawEquipNames(Slot, 0, false, false);
					DrawnSlot = 0xFF;
					Flags[MenuFlag::SubCommands] = 1;
				}
				else if (SubCommand == AbilitiesCommand)
				{
					co_await AbilitiesScreen(Slot, false, 0);
					co_await CloseAbilities();
					DrawnSlot = 0xFF;
				}
				else
				{
					co_await SkillsScreen(Slot);
				}
				// Back to the sheet as it was.
				ShowSheetLabels(true);
				Flags[MenuFlag::StatusSheet] = 1;
				Flags[MenuFlag::Stats] = 1;
				Flags[MenuFlag::EquipNames] = 1;
				DrawSelection(SubCommand + InnerArc);
				Flags[MenuFlag::Pointer] = 1;
				DrawnSubCommand = 0xFF;
				DrawMemberHints(0, false);
				break;
			case EMenuCode::Cancel:
				bGoesOn = false;
				break;
			case EMenuCode::R1:
				Slot = NextMember(Slot, false, false);
				break;
			case EMenuCode::L1:
				Slot = NextMember(Slot, true, false);
				break;
			default:
				break;
			}
			if (!bGoesOn)
			{
				ShowSheetLabels(false);
				Flags[MenuFlag::Pointer] = 0;
				Flags[MenuFlag::Selection] = 0;
				co_return true;
			}
		}
	}

	void FMenuMain::OpenStatus()
	{
		ShowSheetLabels(true);
		DrawMemberHints(0, false);
	}

	void FMenuMain::ShowSheetLabels(bool bShown)
	{
		if (!bShown)
		{
			ClearFlags(MenuFlag::SheetLabels, FirstHeading);
			return;
		}
		const FMenuMainTables& Tables = Resources.Main();
		RenderLabels(SheetLabels, Tables.SheetTexts, SheetLabelsBase);
		for (int32 Heading = 0; Heading < 3; ++Heading)
		{
			FMenuLabel& Label = SheetLabels[static_cast<std::size_t>(FirstHeading + Heading)];
			SetLayerQuad(Label.Quad.Points, Tables.SheetHeadingX[static_cast<std::size_t>(Heading)], Tables.SheetHeadingY[static_cast<std::size_t>(Heading)],
				Label.Width, LabelRows);
			Flags[static_cast<std::size_t>(MenuFlag::SheetLabels + FirstHeading + Heading)] = 1;
		}
	}

	void FMenuMain::PlaceSheetSubLabel()
	{
		// The selected inner command's text label is placed beside its orb, but never drawn: only the
		// headings are drawn on the layer.
		ClearFlags(MenuFlag::SheetLabels, FirstHeading);
		const FMenuMainTables& Tables = Resources.Main();
		FMenuLabel& Label = SheetLabels[static_cast<std::size_t>(SubCommand)];
		const int16 Left = static_cast<int16>(Tables.ArcX[static_cast<std::size_t>(InnerArc + SubCommand)] +
			Tables.SheetSubLabelOffsets[static_cast<std::size_t>(SubCommand)] + SubLabelX);
		const int16 Top = static_cast<int16>(Tables.ArcY[static_cast<std::size_t>(InnerArc + SubCommand)] + SubLabelY);
		Label.Quad.Corners[0].X = Left;
		Label.Quad.Corners[0].Y = Top;
		Label.Quad.Corners[1].X = static_cast<int16>(Left + Label.Width);
		Label.Quad.Corners[1].Y = Top;
		Label.Quad.Corners[2].X = Left;
		Label.Quad.Corners[2].Y = static_cast<int16>(Top + LabelRows);
		Label.Quad.Corners[3].X = static_cast<int16>(Left + Label.Width);
		Label.Quad.Corners[3].Y = static_cast<int16>(Top + LabelRows);
		Flags[static_cast<std::size_t>(MenuFlag::SheetLabels + SubCommand)] = 1;
	}

	void FMenuMain::DrawMemberHints(int32 Kind, bool bGears)
	{
		// Only when there is someone else to change to.
		if ((bGears ? MembersWithGears : Members) == 1)
		{
			return;
		}
		const FMenuMainTables& Tables = Resources.Main();
		const int32 Y = Tables.MemberHintY[static_cast<std::size_t>(Kind)];
		MemberHints.clear();
		Resources.Sheet().Draw(PreviousHint, Tables.MemberHintX[0], Y, FullScale, MemberHints);
		MemberHints.resize(HintParts);
		Resources.Sheet().Draw(NextHint, Tables.MemberHintX[1], Y, FullScale, MemberHints);
		MemberHints.resize(2 * HintParts);
		SetLayerSprites(MemberHints);
		Flags[MenuFlag::MemberHintsMade] = 1;
		Flags[MenuFlag::MemberHints] = 1;
	}

	int32 FMenuMain::NextMember(int32 Slot, bool bPrevious, bool bGearsOnly) const
	{
		// Round the party's three slots to the next occupied one (with a gear, for the gear screens).
		int32 Next = Slot;
		do
		{
			Next = bPrevious ? (Next - 1 < 0 ? 2 : Next - 1) : (Next + 1 > 2 ? 0 : Next + 1);
		} while (bGearsOnly ? Flags[static_cast<std::size_t>(MenuFlag::PartyHasGear + Next)] == 0 : Member(Next) == FGameState::EmptySlot);
		return Next;
	}

	TMenuTask<> FMenuMain::CloseCommand(int32 Offset)
	{
		switch (Command + Offset)
		{
		case FileArcCommand:
		case ContinueCommand:
			CloseFileScreen();
			break;
		case StatusCommand:
			Flags[MenuFlag::StatusSheet] = 0;
			Flags[MenuFlag::Stats] = 0;
			Flags[MenuFlag::EquipNames] = 0;
			ClearFlags(MenuFlag::SheetLabels, static_cast<int32>(SheetLabels.size()));
			break;
		case ItemsCommand:
			CloseItems();
			break;
		case AbilitiesArcCommand:
			co_await CloseAbilities();
			break;
		case GearArcCommand:
			Flags[MenuFlag::StatusSheet] = 0;
			Flags[MenuFlag::Stats] = 0;
			Flags[MenuFlag::EquipNames] = 0;
			break;
		case EquipArcCommand:
			co_await CloseEquip();
			Flags[MenuFlag::Stats] = 0;
			Flags[MenuFlag::EquipNames] = 0;
			co_await Frame();
			break;
		default:
			break;
		}
	}

	void FMenuMain::AddMemberHints()
	{
		if (Flags[MenuFlag::MemberHints] != 0)
		{
			AddLayered(MemberHints);
		}
	}
}
