#include "Menu/MenuMain.h"

#include "Menu/OptionsText.h"

#include <vector>

namespace Weltall
{
	namespace
	{
		/** The rows the screen adds after the Modding page's own. */
		enum class EModdingRow : uint8
		{
			Heading,
			Assets,
			PerfectWorks,
		};

		/** The added rows: the mods' heading over the Assets page's row and each mod's, those the game has. */
		std::vector<EModdingRow> ModdingRows(const IMenuSettings& Settings)
		{
			std::vector<EModdingRow> Rows;
			const bool bAssets = Settings.Assets() != nullptr;
			const bool bPerfectWorks = Settings.Mods().bPerfectWorks;
			if (bAssets || bPerfectWorks)
			{
				Rows.push_back(EModdingRow::Heading);
			}
			if (bAssets)
			{
				Rows.push_back(EModdingRow::Assets);
			}
			if (bPerfectWorks)
			{
				Rows.push_back(EModdingRow::PerfectWorks);
			}
			return Rows;
		}
	}

	bool FMenuMain::OnModdingTurned() const
	{
		return OptionsState.Turned == TurnedPages() - 1;
	}

	void FMenuMain::AddModdingRows()
	{
		namespace Text = OptionsText::Modding;
		for (const EModdingRow Row : ModdingRows(*Services.Settings))
		{
			switch (Row)
			{
			case EModdingRow::Heading:
			{
				FSettingsRow Heading;
				Heading.Label = std::string(Text::Mods);
				Heading.bHeading = true;
				OptionsState.Rows.push_back(std::move(Heading));
				break;
			}
			case EModdingRow::Assets:
				OptionsState.Rows.push_back({std::string(Text::Assets.Label), {}, OptionsText::Fill(Text::Assets.Help)});
				break;
			case EModdingRow::PerfectWorks:
			{
				const std::string Version = Services.Settings->Mods().PerfectWorksVersion;
				std::array<std::string, 3> Help = OptionsText::Fill(Text::PerfectWorks.Help, {Version});
				if (Version.empty())
				{
					Help[0] = std::string(Text::PerfectWorksUnversioned);
				}
				const bool bOn = OptionsState.Settings.PerfectWorks.bEnabled;
				OptionsState.Rows.push_back({std::string(Text::PerfectWorks.Label), std::string(bOn ? OptionsText::On : OptionsText::Off), Help});
				break;
			}
			}
		}
	}

	bool FMenuMain::StepModdingRow()
	{
		FMenuOptionsState& State = OptionsState;
		const std::vector<EModdingRow> Rows = ModdingRows(*Services.Settings);
		const std::size_t Added = static_cast<std::size_t>(State.Row - FSettingsPages::RowCount(ESettingsPage::Modding));
		if (Added >= Rows.size() || Rows[Added] != EModdingRow::PerfectWorks)
		{
			return false;
		}
		State.Settings.PerfectWorks.bEnabled = !State.Settings.PerfectWorks.bEnabled;
		Services.Settings->Change(State.Settings);
		FillOptionsRows();
		return true;
	}

	bool FMenuMain::ActivateModdingRow()
	{
		FMenuOptionsState& State = OptionsState;
		const std::vector<EModdingRow> Rows = ModdingRows(*Services.Settings);
		const std::size_t Added = static_cast<std::size_t>(State.Row - FSettingsPages::RowCount(ESettingsPage::Modding));
		if (Added >= Rows.size() || Rows[Added] == EModdingRow::Heading)
		{
			return false;
		}
		State.OpenedFrom = State.Row;
		if (Rows[Added] == EModdingRow::Assets)
		{
			State.bAssetsOpened = true;
		}
		else
		{
			State.Opened = ESettingsPage::PerfectWorks;
		}
		ShowOptionsPage(true);
		return true;
	}
}
