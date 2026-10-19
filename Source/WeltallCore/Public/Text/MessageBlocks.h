#pragma once

#include "Foundation/Types.h"

#include <string>
#include <string_view>
#include <vector>

namespace Weltall
{
	/**
	 * A message as a text file of messages gives it:
	 *
	 *   message field/4/12        ; 48 x 3
	 *   {
	 *   Fei
	 *   "Hey, Alice!"
	 *   }
	 *
	 * The header names the message's id; what follows a semicolon is a comment. Every line between the braces is the
	 * message's text as it is, in MesText's form (MesText.h), the lines' breaks its own; a brace alone on its line ends
	 * it. Outside the blocks a file holds blank lines and comments, lines whose first character is a semicolon.
	 */
	struct FMessageBlock
	{
		std::string Id;
		std::string Text;
		/** The header's line in its file, from 1. */
		int32 Line = 0;
	};

	/** A line of a file of messages that is no part of a block, or a block that never ends. */
	struct FMessageFileProblem
	{
		int32 Line = 0;
		std::string Message;
	};

	/** Adds a block for the message to File, Comment after its header where it is not empty. */
	WELTALLCORE_API void AppendMessageBlock(std::string& File, std::string_view Id, std::string_view Text, std::string_view Comment = {});

	/** The blocks of a file in their order; what is no part of one is a problem, and the reading goes on after it. */
	WELTALLCORE_API std::vector<FMessageBlock> ParseMessageBlocks(std::string_view File, std::vector<FMessageFileProblem>& Problems);
}
