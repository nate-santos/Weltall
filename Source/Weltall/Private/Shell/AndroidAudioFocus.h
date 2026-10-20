#pragma once

#include "CoreMinimal.h"

#if PLATFORM_ANDROID

enum class EWeltallAudioFocus : uint8
{
	Inactive, Granted, TransientLoss, PermanentLoss, Waiting, Denied
};

struct FWeltallAudioFocusSnapshot
{
	EWeltallAudioFocus State = EWeltallAudioFocus::Denied;
	uint32 Losses = 0;
};

void SetAndroidAudioActive(bool bActive);
FWeltallAudioFocusSnapshot ReadAndroidAudioFocus();
void ResumeAndroidAudio();

#endif
