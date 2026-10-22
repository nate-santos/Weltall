#pragma once

#include "CoreMinimal.h"
#include "Foundation/ByteSource.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/ScopeLock.h"

/** Read-only UFS/PAK source. The handle's seek position is never shared without its lock. */
class FPlatformByteSource final : public Weltall::IByteSource
{
public:
	static std::shared_ptr<const FPlatformByteSource> Open(const FString& Path)
	{
		TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path));
		if (!File || File->Size() < 0) return {};
		return std::shared_ptr<const FPlatformByteSource>(new FPlatformByteSource(MoveTemp(File)));
	}

	Weltall::uint64 Size() const override { return Length; }

	bool Read(Weltall::uint64 Offset, std::span<Weltall::uint8> Destination) const override;

private:
	explicit FPlatformByteSource(TUniquePtr<IFileHandle> InFile) : File(MoveTemp(InFile)), Length(File->Size()) {}
	TUniquePtr<IFileHandle> File;
	uint64 Length;
	mutable FCriticalSection Mutex;
};
