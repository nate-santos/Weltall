#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"

struct FBundledDiscsProgress;

/** Locally bundled Android data is unpacked asynchronously into the app's native filesystem. */
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

private:
	TSharedPtr<FBundledDiscsProgress, ESPMode::ThreadSafe> Progress;
	TFuture<bool> Task;
	bool bFailed = false;
};
