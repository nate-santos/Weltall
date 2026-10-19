#pragma once

#include "Foundation/Result.h"
#include "Foundation/Types.h"
#include "Text/MesFontSheet.h"

#include <filesystem>
#include <memory>

namespace Weltall
{
	class FIso9660;
	class FGameFileIndex;

	/**
	 * Writes the disc's text as files of messages below a text folder's originals (Assets/text/original: GameText.h's
	 * files) and the font's table below a font folder's (Assets/font/original/font.tsv, MesFontSheet.h), the disc's own
	 * whatever replaces its files. A file already there is kept.
	 */
	class FGameTextExport
	{
	public:
		WELTALLCORE_API static TResult<std::unique_ptr<FGameTextExport>> Open(const FIso9660& Disc, const FGameFileIndex& Index, std::filesystem::path TextFolder,
			std::filesystem::path FontFolder);
		WELTALLCORE_API ~FGameTextExport();

		/** Writes the next few files; true once every file is written. */
		WELTALLCORE_API TResult<bool> Step();

		WELTALLCORE_API int32 CompletedFiles() const;
		WELTALLCORE_API int32 TotalFiles() const;

		/** The font sheet's picture, which the host saves beside its table (font.png) as it saves pictures. */
		WELTALLCORE_API const FRgbaPicture& FontPicture() const;

	private:
		struct FState;
		explicit FGameTextExport(std::unique_ptr<FState> InState);
		std::unique_ptr<FState> State;
	};
}
