#pragma once

#include "Foundation/Result.h"
#include "Foundation/Types.h"

#include <span>
#include <string>
#include <vector>

namespace Weltall
{
	class FCdImage;
	class FGameFileIndex;
	class FPsxExecutable;

	/** A message of the game's text: its codes, its 00 included, and the window its table gives it. */
	struct FGameTextMessage
	{
		std::vector<uint8> Codes;
		int32 WidthUnits = 0;
		int32 Lines = 0;
	};

	/** One of the game's tables of text, as its reader takes it from the disc. */
	struct FGameTextTable
	{
		/** Its name in its messages' ids (MessageTableIds.h), and what it holds. */
		std::string Table;
		std::string Label;
		std::vector<FGameTextMessage> Messages;
		/** Its first message's number: 0, or 1 for the chapters. */
		int32 First = 0;
		/** Whether its sizes are its messages' windows (a field map's), which the export writes beside each message. */
		bool bWindowSizes = false;
	};

	/** The kinds of file the text export writes, each a kind of table. */
	enum class EGameTextFile : uint8
	{
		Field,
		Names,
		Menu,
		MenuHelp,
		Battle,
		BattleHelp,
		Enemies,
		BattleEvents,
		WorldMap,
		Battling,
		Chapters,
	};

	/** A file of the text export: its kind, and for a field map's its map. */
	struct FGameTextFile
	{
		EGameTextFile Kind = EGameTextFile::Field;
		int32 Map = 0;
	};

	/** Every file the export writes, in its order: each field map's, then one of each other kind. */
	WELTALLCORE_API std::vector<FGameTextFile> GameTextFiles();

	/** Where a file goes below the export's folder: field/004.txt, names.txt and so on. */
	WELTALLCORE_API std::string GameTextFilePath(FGameTextFile File);

	/**
	 * The tables a file holds, read from the image as their readers read them (the image's asset store and mods are its
	 * own affair: the export gives it the disc alone). None for a field map the disc does not hold.
	 */
	WELTALLCORE_API TResult<std::vector<FGameTextTable>> ReadGameText(const FCdImage& Image, const FGameFileIndex& Index, const FPsxExecutable& Executable,
		FGameTextFile File);

	/** A file of messages (MessageBlocks.h) holding every message of Tables, in the font's characters. */
	WELTALLCORE_API std::string WriteGameText(FGameTextFile File, std::span<const FGameTextTable> Tables);
}
