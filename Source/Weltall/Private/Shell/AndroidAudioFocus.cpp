#include "Shell/AndroidAudioFocus.h"

#if PLATFORM_ANDROID

#include "Android/AndroidApplication.h"
#include "WeltallLog.h"

namespace
{
	struct FWeltallAudioActivity
	{
		JNIEnv* Env = FAndroidApplication::GetJavaEnv();
		jobject Activity = FAndroidApplication::GetGameActivityThis();
		jclass Class = Env && Activity ? Env->GetObjectClass(Activity) : nullptr;
		~FWeltallAudioActivity() { if (Class) Env->DeleteLocalRef(Class); }
		bool Failed()
		{
			if (Env && !Env->ExceptionCheck()) return false;
			if (Env) Env->ExceptionClear();
			static bool bReported = false;
			if (!bReported)
			{
				UE_LOG(LogWeltall, Error, TEXT("Android audio focus could not be accessed; the game remains paused"));
				bReported = true;
			}
			return true;
		}
		jmethodID Method(const char* Name, const char* Signature)
		{
			if (Failed() || !Class) return nullptr;
			jmethodID Result = Env->GetMethodID(Class, Name, Signature);
			return Failed() ? nullptr : Result;
		}
	};
}

void SetAndroidAudioActive(bool bActive)
{
	FWeltallAudioActivity Java;
	if (jmethodID Method = Java.Method("AndroidThunkJava_WeltallSetAudioActive", "(Z)V"))
	{
		Java.Env->CallVoidMethod(Java.Activity, Method, bActive ? JNI_TRUE : JNI_FALSE);
		Java.Failed();
	}
}

FWeltallAudioFocusSnapshot ReadAndroidAudioFocus()
{
	FWeltallAudioActivity Java;
	if (jmethodID Method = Java.Method("AndroidThunkJava_WeltallAudioFocusSnapshot", "()J"))
	{
		const uint64 Packed = static_cast<uint64>(Java.Env->CallLongMethod(Java.Activity, Method));
		if (!Java.Failed() && (Packed & 0xFFFFFFFFu) <= static_cast<uint64>(EWeltallAudioFocus::Denied))
		{
			return {static_cast<EWeltallAudioFocus>(Packed & 0xFFFFFFFFu), static_cast<uint32>(Packed >> 32)};
		}
	}
	return {};
}

void ResumeAndroidAudio()
{
	FWeltallAudioActivity Java;
	if (jmethodID Method = Java.Method("AndroidThunkJava_WeltallResumeAudio", "()V"))
	{
		Java.Env->CallVoidMethod(Java.Activity, Method);
		Java.Failed();
	}
}

#endif
