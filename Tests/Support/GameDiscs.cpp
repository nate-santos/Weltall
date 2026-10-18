#include "Support/GameDiscs.h"

#include "Media/GameLibrary.h"

#include <cstdlib>
#include <string_view>

namespace Weltall::Testing
{
	const FIso9660* FindDisc(EDiscRelease Release)
	{
		// Synthetic device runs must not discover discs cached by an earlier content run.
#if defined(_MSC_VER)
		char* NoDiscs = nullptr;
		std::size_t Length = 0;
		const bool bRead = _dupenv_s(&NoDiscs, &Length, "WELTALL_TEST_NO_DISCS") == 0;
		const bool bDisabled = bRead && NoDiscs != nullptr && std::string_view(NoDiscs) == "1";
		std::free(NoDiscs);
		if (bDisabled) return nullptr;
#else
		const char* NoDiscs = std::getenv("WELTALL_TEST_NO_DISCS");
		if (NoDiscs != nullptr && std::string_view(NoDiscs) == "1") return nullptr;
#endif
		static const FGameLibrary Library = FGameLibrary::Scan(WELTALL_GAMES_DIR);
		return Library.Find(Release);
	}
}
