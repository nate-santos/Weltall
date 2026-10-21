#pragma once

#include "Menu/MenuSettingsPages.h"
#include "Menu/OptionsText.h"

#include <array>
#include <string>
#include <string_view>

/** The pieces the options pages make their rows of. */
namespace Weltall::SettingsRows
{
	/** A row's setting among choices named in order. */
	template <typename TEnum>
	bool StepChoice(TEnum& Value, int32 Direction, int32 Count)
	{
		Value = static_cast<TEnum>((static_cast<int32>(Value) + Direction + Count) % Count);
		return true;
	}

	template <typename TEnum, std::size_t Count>
	std::string ChoiceName(TEnum Value, const std::array<std::string_view, Count>& Names)
	{
		return std::string(Names[static_cast<std::size_t>(Value)]);
	}

	inline std::string OnOff(bool bOn)
	{
		return std::string(bOn ? OptionsText::On : OptionsText::Off);
	}

	/** A row as the screen shows it: the words' label and help, and its value. */
	inline FSettingsRow Shown(const OptionsText::FRow& Text, std::string Value)
	{
		return {std::string(Text.Label), std::move(Value), OptionsText::Fill(Text.Help)};
	}

	inline FSettingsRow Heading(std::string_view Label)
	{
		FSettingsRow Row;
		Row.Label = std::string(Label);
		Row.bHeading = true;
		return Row;
	}
}
