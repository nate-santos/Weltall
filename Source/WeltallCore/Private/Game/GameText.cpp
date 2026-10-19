#include "Game/GameText.h"

#include "Battle/BattleEvent.h"
#include "Battle/BattleScene.h"
#include "Battle/Show/BattleSceneGraphics.h"
#include "Battle/Show/BattleShowHud.h"
#include "Battling/BattlingData.h"
#include "Field/FieldMap.h"
#include "Foundation/LittleEndian.h"
#include "Game/ChapterTitles.h"
#include "Menu/MenuResources.h"
#include "Text/MesCharacters.h"
#include "Text/MesResident.h"
#include "Text/MesText.h"
#include "Text/MessageBlocks.h"
#include "Text/MessageTableIds.h"
#include "WorldMap/WorldMapScene.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <tuple>

namespace Weltall
{
	namespace
	{
		/** A battle scene's word naming where its name table is. */
		constexpr std::size_t SceneNameTableOffset = 0x30;
		/** The world map's scene groups whose tables it shows: one an era. The cutscenes' groups hold the Japanese release's names. */
		constexpr int32 WorldMapEras = 9;

		/** What each of the kernel's name tables holds, by its number; empty for the ones that hold none. */
		constexpr std::array<std::string_view, 53> NameTableLabels = {
			"Fei's Deathblows", "Elly's Deathblows", "Citan's Deathblows", "Bart's Deathblows", "Billy's Deathblows", "Rico's Deathblows",
			"Emeralda's Deathblows", "Chu-chu's (Japanese placeholders)", "Maria's", "Citan's Deathblows with his sword",
			"the adult Emeralda's Deathblows", "", "", "", "", "", "", "armor and accessories", "the battle's labels", "",
			"Ether and Chi abilities", "", "items", "weapons", "Japanese placeholders", "Japanese placeholders", "the party's names in Japanese",
			"", "a gear's Deathblows", "a gear's Deathblows", "a gear's Deathblows", "a gear's Deathblows", "a gear's Deathblows",
			"a gear's Deathblows", "a gear's Deathblows", "a gear's Deathblows", "a gear's Deathblows", "a gear's Deathblows",
			"a gear's Deathblows", "a gear's Deathblows", "a gear's Deathblows", "a gear's Deathblows", "a gear's Deathblows",
			"a gear's Deathblows", "a gear's Deathblows", "a gear's Deathblows", "a gear's Deathblows", "", "abilities by character",
			"the buttons' names (Japanese)", "gear armor and parts", "gear weapons", "gear abilities",
		};

		/** What each kind of the Menu's descriptions holds, in FMenuResources::EText's order. */
		constexpr std::array<std::string_view, static_cast<std::size_t>(FMenuResources::EText::Count)> MenuHelpLabels = {
			"the items' descriptions", "the characters' abilities' help", "a gear's abilities' help", "a gear's own abilities' help",
			"the weapons' descriptions", "the accessories' descriptions", "the gear weapons' descriptions", "the gear parts' descriptions",
			"the weapon shop's lines", "the accessory shop's lines", "the item shop's lines", "the gear shop's weapons", "the gear shop's parts",
			"the gear shop's armors", "the gear shop's frames", "the gear shop's engines", "the gears' armors' names", "the gears' frames' names",
			"the gears' engines' names",
		};

		struct FKindText
		{
			std::string_view Path;
			std::string_view Label;
		};

		constexpr std::array<FKindText, 11> Kinds = {{
			{"field/", "a field map's dialogue, signs and prompts"},
			{"names.txt", "the names the kernel keeps: items, weapons, abilities, Deathblows, the battle's labels"},
			{"menu.txt", "the Menu's prompts and labels"},
			{"menu-help.txt", "the Menu's descriptions and names"},
			{"battle.txt", "what a battle's actions say"},
			{"battle-help.txt", "the battle's help lines"},
			{"enemies.txt", "the battle scenes' enemies: their names, then what their scripts say"},
			{"battle-events.txt", "the scripted battles' dialogue"},
			{"worldmap.txt", "the world map's place names and messages"},
			{"battling.txt", "the Battling arena's menus, tutorial and taunts"},
			{"chapters.txt", "the chapters' titles"},
		}};

		FGameTextTable TableOf(std::string Table, std::string Label, const FMessageTable& Messages, bool bWindowSizes = false)
		{
			FGameTextTable Text{std::move(Table), std::move(Label), {}, 0, bWindowSizes};
			for (int32 Index = 0; Index < Messages.Count(); ++Index)
			{
				const std::span<const uint8> String = Messages.String(Index);
				const std::span<const uint8> Codes = String.first(MesMessageSize(String));
				Text.Messages.push_back({std::vector<uint8>(Codes.begin(), Codes.end()), Messages.WidthUnits(Index), Messages.Lines(Index)});
			}
			return Text;
		}

		std::optional<FError> AddTable(std::vector<FGameTextTable>& Tables, std::string Table, std::string Label, std::span<const uint8> Bytes,
			bool bWindowSizes = false)
		{
			const TResult<FMessageTable> Messages = FMessageTable::Parse(Bytes);
			if (!Messages)
			{
				return MakeError(Table + ": " + Messages.Error().Message);
			}
			Tables.push_back(TableOf(std::move(Table), std::move(Label), *Messages, bWindowSizes));
			return std::nullopt;
		}

		TResult<std::vector<FGameTextTable>> ReadField(const FCdImage& Image, const FGameFileIndex& Index, int32 Map)
		{
			std::vector<FGameTextTable> Tables;
			const TResult<FFieldArchive> Archive = LoadFieldArchive(Image, Index, Map);
			if (!Archive)
			{
				return Tables;
			}
			if (std::optional<FError> Error = AddTable(Tables, MessageTables::Field(Map), "field map " + std::to_string(Map),
					Archive->Section(EFieldSection::Messages), true))
			{
				return *Error;
			}
			return Tables;
		}

		TResult<std::vector<FGameTextTable>> ReadNames(const FCdImage& Image, const FGameFileIndex& Index, const FPsxExecutable& Executable)
		{
			const TResult<FMesResident> Resident = LoadMesResident(Image, Index, Executable);
			if (!Resident)
			{
				return Resident.Error();
			}
			std::vector<FGameTextTable> Tables;
			for (int32 Table = 0; Table < Resident->NameTables.Count(); ++Table)
			{
				const TResult<FMessageTable> Messages = FMessageTable::Parse(Resident->NameTables.Table(Table));
				if (Table == FMesNameTables::GlyphMapTable || !Messages || Messages->Count() == 0)
				{
					continue;
				}
				const std::string_view Label = static_cast<std::size_t>(Table) < NameTableLabels.size() ? NameTableLabels[static_cast<std::size_t>(Table)] : "";
				Tables.push_back(TableOf(MessageTables::Names(Table), std::string(Label), *Messages));
			}
			return Tables;
		}

		TResult<std::vector<FGameTextTable>> ReadMenu(const FCdImage& Image, const FGameFileIndex& Index, const FPsxExecutable& Executable, bool bHelp)
		{
			const TResult<FMenuResources> Menu = FMenuResources::Load(Image, Index, Executable);
			if (!Menu)
			{
				return Menu.Error();
			}
			std::vector<FGameTextTable> Tables;
			if (!bHelp)
			{
				Tables.push_back(TableOf(std::string(MessageTables::Menu), "the Menu's prompts and labels", Menu->Text()));
				return Tables;
			}
			for (std::size_t Kind = 0; Kind < MenuHelpLabels.size(); ++Kind)
			{
				const auto Which = static_cast<FMenuResources::EText>(Kind);
				Tables.push_back(TableOf(MessageTables::MenuHelp(FMenuResources::SectionOfText(Which)), std::string(MenuHelpLabels[Kind]), Menu->Text(Which)));
			}
			return Tables;
		}

		TResult<std::vector<FGameTextTable>> ReadBattle(const FCdImage& Image, const FGameFileIndex& Index, const FPsxExecutable& Executable, bool bHelp)
		{
			const TResult<FBattleHudResources> Hud = FBattleHudResources::Load(Image, Index, Executable);
			if (!Hud)
			{
				return Hud.Error();
			}
			std::vector<FGameTextTable> Tables;
			const std::array<std::tuple<std::string_view, std::string_view, std::span<const uint8>>, 5> Sources = {{
				{MessageTables::Battle, "what a battle's actions say", Hud->Messages},
				{MessageTables::BattleItemHelp, "the items' help in battle", Hud->ItemHelp},
				{MessageTables::BattleEtherHelp, "the Ethers' help on foot", Hud->HelpTexts[0]},
				{MessageTables::BattleGearEtherHelp, "the Ethers' help in a gear", Hud->HelpTexts[1]},
				{MessageTables::BattleSpecialHelp, "the gears' Specials' help", Hud->HelpTexts[2]},
			}};
			for (std::size_t Source = bHelp ? 1 : 0; Source < (bHelp ? Sources.size() : 1); ++Source)
			{
				const auto& [Table, Label, Bytes] = Sources[Source];
				if (std::optional<FError> Error = AddTable(Tables, std::string(Table), std::string(Label), Bytes))
				{
					return *Error;
				}
			}
			return Tables;
		}

		TResult<std::vector<FGameTextTable>> ReadEnemies(const FCdImage& Image, const FGameFileIndex& Index)
		{
			std::vector<FGameTextTable> Tables;
			for (const int32 Scene : FBattleSceneGraphics::Scenes(Index))
			{
				const TResult<FBattleScene> Loaded = FBattleScene::Load(Image, Index, Scene);
				if (!Loaded)
				{
					return Loaded.Error();
				}
				const std::span<const uint8> File = Loaded->Bytes();
				const std::size_t At = File.size() >= SceneNameTableOffset + 2 ? LittleEndian::ReadU16(File, SceneNameTableOffset) : 0;
				if (At == 0 || At >= File.size())
				{
					continue;
				}
				if (std::optional<FError> Error = AddTable(Tables, MessageTables::Enemies(Scene), "battle scene " + std::to_string(Scene), File.subspan(At)))
				{
					return *Error;
				}
			}
			return Tables;
		}

		TResult<std::vector<FGameTextTable>> ReadBattleEvents(const FCdImage& Image, const FGameFileIndex& Index)
		{
			const TResult<FBattleEventArchive> Archive = FBattleEventArchive::Load(Image, Index);
			if (!Archive)
			{
				return Archive.Error();
			}
			std::vector<FGameTextTable> Tables;
			for (int32 Event = 0; Event < Archive->Count(); ++Event)
			{
				if (std::optional<FError> Error = AddTable(Tables, MessageTables::BattleEvent(Event), "battle event " + std::to_string(Event), Archive->Messages(Event)))
				{
					return *Error;
				}
			}
			return Tables;
		}

		TResult<std::vector<FGameTextTable>> ReadWorldMap(const FCdImage& Image, const FGameFileIndex& Index)
		{
			std::vector<FGameTextTable> Tables;
			for (int32 Group = 0; Group < WorldMapEras; ++Group)
			{
				const TResult<std::vector<uint8>> Messages = ReadWorldMapMessages(Image, Index, Group);
				if (!Messages)
				{
					return Messages.Error();
				}
				if (std::optional<FError> Error = AddTable(Tables, MessageTables::WorldMap(Group), "the world map in its era " + std::to_string(Group), *Messages))
				{
					return *Error;
				}
			}
			return Tables;
		}

		TResult<std::vector<FGameTextTable>> ReadBattling(const FCdImage& Image, const FGameFileIndex& Index)
		{
			const TResult<FBattlingMessages> Messages = FBattlingMessages::Read(Image, Index);
			if (!Messages)
			{
				return Messages.Error();
			}
			return std::vector<FGameTextTable>{TableOf(std::string(MessageTables::Battling), "the Battling arena's menus, tutorial and taunts", Messages->Table())};
		}

		TResult<std::vector<FGameTextTable>> ReadChapters(const FCdImage& Image, const FGameFileIndex& Index)
		{
			const TResult<FChapterTitles> Titles = FChapterTitles::Read(Image, Index);
			if (!Titles)
			{
				return Titles.Error();
			}
			FGameTextTable Table{std::string(MessageTables::Chapters), "the chapters' titles, by their numbers", {}, 1, false};
			for (const FChapter& Chapter : Titles->Chapters())
			{
				Table.Messages.push_back({MesCodesOfText(Chapter.Title).value_or(std::vector<uint8>{0}), 0, 0});
			}
			return std::vector<FGameTextTable>{std::move(Table)};
		}
	}

	std::vector<FGameTextFile> GameTextFiles()
	{
		std::vector<FGameTextFile> Files;
		for (int32 Map = 0; Map < FieldMapCount; ++Map)
		{
			Files.push_back({EGameTextFile::Field, Map});
		}
		for (uint8 Kind = static_cast<uint8>(EGameTextFile::Names); Kind <= static_cast<uint8>(EGameTextFile::Chapters); ++Kind)
		{
			Files.push_back({static_cast<EGameTextFile>(Kind), 0});
		}
		return Files;
	}

	std::string GameTextFilePath(FGameTextFile File)
	{
		if (File.Kind == EGameTextFile::Field)
		{
			char Name[16];
			std::snprintf(Name, sizeof(Name), "%03d.txt", static_cast<int>(File.Map));
			return std::string(Kinds[0].Path) + Name;
		}
		return std::string(Kinds[static_cast<std::size_t>(File.Kind)].Path);
	}

	TResult<std::vector<FGameTextTable>> ReadGameText(const FCdImage& Image, const FGameFileIndex& Index, const FPsxExecutable& Executable, FGameTextFile File)
	{
		switch (File.Kind)
		{
		case EGameTextFile::Field: return ReadField(Image, Index, File.Map);
		case EGameTextFile::Names: return ReadNames(Image, Index, Executable);
		case EGameTextFile::Menu: return ReadMenu(Image, Index, Executable, false);
		case EGameTextFile::MenuHelp: return ReadMenu(Image, Index, Executable, true);
		case EGameTextFile::Battle: return ReadBattle(Image, Index, Executable, false);
		case EGameTextFile::BattleHelp: return ReadBattle(Image, Index, Executable, true);
		case EGameTextFile::Enemies: return ReadEnemies(Image, Index);
		case EGameTextFile::BattleEvents: return ReadBattleEvents(Image, Index);
		case EGameTextFile::WorldMap: return ReadWorldMap(Image, Index);
		case EGameTextFile::Battling: return ReadBattling(Image, Index);
		case EGameTextFile::Chapters: return ReadChapters(Image, Index);
		}
		return MakeError("no such kind of text");
	}

	std::string WriteGameText(FGameTextFile File, std::span<const FGameTextTable> Tables)
	{
		std::string Text = "; The game's text as the disc has it: " + std::string(Kinds[static_cast<std::size_t>(File.Kind)].Label) + ".\n"
			"; Copy a message into a .txt of your own below Assets/text (not in this folder) and change its text there:\n"
			"; it replaces the message of that id. Docs/Modding.md explains the marks in braces and the windows' sizes.\n";
		if (std::any_of(Tables.begin(), Tables.end(), [](const FGameTextTable& Table) { return Table.bWindowSizes; }))
		{
			Text += "; After each id: the window's width and lines its table gives it (a letter is 2 wide, a symbol 3).\n";
		}
		for (const FGameTextTable& Table : Tables)
		{
			if (Table.Messages.empty())
			{
				continue;
			}
			Text += "\n; " + Table.Table + (Table.Label.empty() ? "" : ": " + Table.Label) + "\n\n";
			for (std::size_t Message = 0; Message < Table.Messages.size(); ++Message)
			{
				const FGameTextMessage& Shown = Table.Messages[Message];
				const std::string Size = Table.bWindowSizes ? std::to_string(Shown.WidthUnits) + " x " + std::to_string(Shown.Lines) : std::string();
				AppendMessageBlock(Text, MessageTables::MessageId(Table.Table, Table.First + static_cast<int32>(Message)),
					MesTextOfCodes(Shown.Codes, FMesCharacters::Font()), Size);
			}
		}
		return Text;
	}
}
