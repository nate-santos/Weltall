#include "Menu/MenuAssetPages.h"

#include "Menu/OptionsText.h"

#include <algorithm>

namespace Weltall
{
	std::optional<EAssetExportKind> AssetRowExport(int32 Row)
	{
		switch (Row)
		{
		case AssetRow::Textures: return EAssetExportKind::Textures;
		case AssetRow::Text: return EAssetExportKind::Text;
		case AssetRow::Dump: return EAssetExportKind::Dump;
		case AssetRow::Music: return EAssetExportKind::Music;
		default: return std::nullopt;
		}
	}

	std::vector<FSettingsRow> AssetSettingsRows(const FPlayerSettings& Settings, FMenuAssetProgress Progress)
	{
		namespace Text = OptionsText::Assets;
		const bool bRunning = Progress.State == EAssetExportState::Running;
		const auto Action = [&Progress, bRunning](EAssetExportKind Kind, const OptionsText::FRow& Start)
		{
			const OptionsText::FRow& Shown = bRunning && Progress.Kind == Kind ? Text::Cancel : Start;
			FSettingsRow Row{std::string(Shown.Label), {}, OptionsText::Fill(Shown.Help)};
			Row.bReadOnly = bRunning && Progress.Kind != Kind;
			return Row;
		};
		std::vector<FSettingsRow> Rows{
			{std::string(Text::External.Label), std::string(Settings.bExternalAssets ? OptionsText::On : OptionsText::Off), OptionsText::Fill(Text::External.Help)},
			{std::string(Text::Capture.Label), std::string(Settings.bCaptureTextures ? OptionsText::On : OptionsText::Off), OptionsText::Fill(Text::Capture.Help)},
			Action(EAssetExportKind::Textures, Text::Textures),
			Action(EAssetExportKind::Text, Text::Text),
			Action(EAssetExportKind::Dump, Text::Export),
			Action(EAssetExportKind::Music, Text::Music),
			{std::string(Text::Progress.Label), std::string(Text::States[static_cast<std::size_t>(Progress.State)]), OptionsText::Fill(Text::Progress.Help)},
		};
		Rows[AssetRow::Status].bReadOnly = true;
		if (bRunning && Progress.Total != 0)
		{
			const uint64 Done = std::min(Progress.Completed, Progress.Total);
			Rows[AssetRow::Status].Value = Progress.Kind == EAssetExportKind::Music
				? "WAV " + std::to_string(Done) + "/" + std::to_string(Progress.Total)
				: std::to_string(Done * 100 / Progress.Total) + "%";
		}
		if (Progress.State == EAssetExportState::Failed)
		{
			Rows[AssetRow::Status].Help = {"Couldn't finish the export.", "Check space and permissions.", "Details are in the game log."};
		}
		return Rows;
	}
}
