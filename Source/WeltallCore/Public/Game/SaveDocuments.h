#pragma once

#include "Foundation/Types.h"

#include <span>
#include <string>
#include <vector>

namespace Weltall
{
	enum class ESaveDocumentStatus : uint8
	{
		Idle = 0, Pending = 1, Imported = 2, Exported = 3, Cancelled = 4, Failed = 5, TooLarge = 6,
	};

	struct FSaveDocumentResult
	{
		ESaveDocumentStatus Status = ESaveDocumentStatus::Idle;
		std::vector<uint8> Bytes;
	};

	/** A platform document picker. Results are consumed on the game thread; no content URI is a filename. */
	class ISaveDocuments
	{
	public:
		virtual ~ISaveDocuments() = default;
		virtual bool Import() = 0;
		/** Copies Bytes before returning. Export creates a new document chosen by the player. */
		virtual bool Export(std::span<const uint8> Bytes, const std::string& SuggestedName) = 0;
		virtual FSaveDocumentResult Poll() = 0;
		/** Discards a pending result without modifying any game saves. */
		virtual void Cancel() = 0;
	};
}
