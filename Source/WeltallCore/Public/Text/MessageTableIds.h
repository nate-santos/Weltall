#pragma once

#include "Foundation/Types.h"

#include <string>
#include <string_view>

namespace Weltall::MessageTables
{
	/**
	 * The names the player's messages give the game's tables of text (Docs/Modding.md): a message's id is its table's
	 * name, a slash and its number in the table, from 0 ("field/4/12", "names/22/3"). The same on both discs.
	 */

	/** A field map's dialogue, signs and prompts, by the map's number. */
	inline std::string Field(int32 Map) { return "field/" + std::to_string(Map); }

	/** A table of the names the kernel keeps (items, weapons, abilities, Deathblows, battle labels), by its number. */
	inline std::string Names(int32 Table) { return "names/" + std::to_string(Table); }

	/** The Menu's prompts and labels: memory cards, saving, the shops, the party. */
	inline constexpr std::string_view Menu = "menu";

	/** The Menu's descriptions and names of one kind, by the section of the menus' data that holds them, from 1. */
	inline std::string MenuHelp(int32 Section) { return "menu-help/" + std::to_string(Section); }

	/** What a battle's actions say: the statuses, "Counterattack!", the gears' fuel. */
	inline constexpr std::string_view Battle = "battle";

	/** The battle's help lines: the items, the Ethers on foot and in a gear, the gears' Specials. */
	inline constexpr std::string_view BattleItemHelp = "battle-help/items";
	inline constexpr std::string_view BattleEtherHelp = "battle-help/ethers";
	inline constexpr std::string_view BattleGearEtherHelp = "battle-help/gear-ethers";
	inline constexpr std::string_view BattleSpecialHelp = "battle-help/specials";

	/** A battle scene's enemies: their names, then what their scripts say. */
	inline std::string Enemies(int32 Scene) { return "enemies/" + std::to_string(Scene); }

	/** A scripted battle's dialogue, by its event's number. */
	inline std::string BattleEvent(int32 Event) { return "battle-events/" + std::to_string(Event); }

	/** A world map scene's place names and messages, by its group (0 the first of the directory's). */
	inline std::string WorldMap(int32 Group) { return "worldmap/" + std::to_string(Group); }

	/** The Battling arena's menus, tutorial and taunts. */
	inline constexpr std::string_view Battling = "battling";

	/** The chapters' titles, by the chapter's number from 1. */
	inline constexpr std::string_view Chapters = "chapters";

	/** A message's id: its table's name and its number. */
	inline std::string MessageId(std::string_view Table, int32 Index) { return std::string(Table) + "/" + std::to_string(Index); }
}
