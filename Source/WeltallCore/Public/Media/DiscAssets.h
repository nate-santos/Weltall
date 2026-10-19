#pragma once

#include "Media/GameFileIndex.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Weltall
{
	class FMesFontSheet;
	class FMessageOverrides;
	class FMusicReplacements;

	struct FDiscAssetExportStatus
	{
		uint32 CompletedFiles = 0;
		uint32 TotalFiles = 0;
		uint64 CompletedBytes = 0;
		uint64 TotalBytes = 0;
		bool bComplete = false;
		bool bCancelled = false;
		std::string CurrentFile;
	};

	/** A bounded, cancellable copy of original disc assets. Existing files are always preserved. */
	class FDiscAssetExport
	{
	public:
		WELTALLCORE_API ~FDiscAssetExport();
		/** Copies at most MaxBytes, rounded up to one sector. True when complete or cancelled. */
		WELTALLCORE_API TResult<bool> Step(uint64 MaxBytes = 1024 * 1024);
		WELTALLCORE_API void Cancel();
		WELTALLCORE_API const FDiscAssetExportStatus& Status() const;

	private:
		friend class FDiscAssetStore;
		struct FState;
		explicit FDiscAssetExport(std::unique_ptr<FState> InState);
		std::unique_ptr<FState> State;
	};

	/** Native files and optional converted replacements, isolated by disc release. */
	class FDiscAssetStore
	{
	public:
		/** SourceId is a stable release name, such as usa.disc1. Does not create or change files. */
		WELTALLCORE_API static TResult<std::shared_ptr<FDiscAssetStore>> Open(const std::filesystem::path& AssetsRoot,
			std::string SourceId, const FCdImage& Image, const FGameFileIndex& Index);
		WELTALLCORE_API ~FDiscAssetStore();
		WELTALLCORE_API void SetEnabled(bool bEnabled);
		WELTALLCORE_API bool IsEnabled() const;
		WELTALLCORE_API std::filesystem::path Folder() const;
		/** Where a native file's replacement is: files/DD/NNNN.bin or records/RRRRRR.bin below the store's folder. */
		WELTALLCORE_API std::filesystem::path AssetPath(const FIndexRecord& Record) const;
		WELTALLCORE_API TResult<std::optional<std::vector<uint8>>> ReadOverride(const FIndexRecord& Record) const;
		/** The music recordings the store reads while enabled, shared by every disc's store. */
		WELTALLCORE_API void SetMusic(std::shared_ptr<const FMusicReplacements> Music);
		WELTALLCORE_API TResult<std::optional<std::vector<uint8>>> ReadMusic(uint64 Key) const;
		/** The player's messages the store gives while enabled, shared by every disc's store. */
		WELTALLCORE_API void SetMessages(std::shared_ptr<const FMessageOverrides> Messages);
		/** The player's messages while the store is enabled; none otherwise. */
		WELTALLCORE_API std::shared_ptr<const FMessageOverrides> Messages() const;
		/** The player's font sheet the store gives while enabled, shared by every disc's store. */
		WELTALLCORE_API void SetFontSheet(std::shared_ptr<const FMesFontSheet> Sheet);
		/** The player's font sheet while the store is enabled; none otherwise. */
		WELTALLCORE_API std::shared_ptr<const FMesFontSheet> FontSheet() const;
		WELTALLCORE_API TResult<bool> ReadMovieSector(uint32 Lba, std::span<uint8> Destination) const;
		/** Creates the manifest and an incremental export job; refuses a different source layout. */
		WELTALLCORE_API TResult<std::unique_ptr<FDiscAssetExport>> Export() const;

	private:
		struct FState;
		explicit FDiscAssetStore(std::unique_ptr<FState> InState);
		std::unique_ptr<FState> State;
	};
}
