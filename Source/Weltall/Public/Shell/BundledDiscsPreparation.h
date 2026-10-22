#pragma once

#include "CoreMinimal.h"
#include "Media/GameLibrary.h"
#include "Async/Future.h"

struct FBundledDiscsProgress;

/** Verifies/opens bundled data asynchronously; retains extraction for comparison and external readers. */
class FBundledDiscsPreparation
{
public:
	~FBundledDiscsPreparation();
	static FString GamesFolder();
	void Begin();
	void Stop();
	void Poll();
	bool IsBlocked() const;
	FText Notice() const;
	bool UsesPackagedDiscs() const { return bDirect; }
	Weltall::FGameLibrary TakeLibrary();

private:
	TSharedPtr<FBundledDiscsProgress, ESPMode::ThreadSafe> Progress;
	TFuture<bool> Task;
	bool bFailed = false;
	bool bDirect = false;
};
