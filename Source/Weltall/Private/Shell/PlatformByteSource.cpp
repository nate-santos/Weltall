#include "Shell/PlatformByteSource.h"

#include "ProfilingDebugging/CsvProfiler.h"

CSV_DEFINE_CATEGORY(WeltallDiscIO, true);

bool FPlatformByteSource::Read(Weltall::uint64 Offset, std::span<Weltall::uint8> Destination) const
{
	if (Offset > Length || Destination.size() > Length - Offset) return false;
	if (Destination.empty()) return true;
	CSV_SCOPED_TIMING_STAT(WeltallDiscIO, Read);
	FScopeLock Lock(&Mutex);
	CSV_CUSTOM_STAT(WeltallDiscIO, Requests, 1, ECsvCustomStatOp::Accumulate);
	CSV_CUSTOM_STAT(WeltallDiscIO, RequestedBytes, Destination.size(), ECsvCustomStatOp::Accumulate);
	return File->Seek(static_cast<int64>(Offset)) && File->Read(Destination.data(), static_cast<int64>(Destination.size()));
}
