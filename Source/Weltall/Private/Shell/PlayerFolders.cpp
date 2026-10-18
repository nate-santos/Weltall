#include "Shell/PlayerFolders.h"

#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#endif

namespace
{
	/** An XDG base directory: its variable, or its default under the user's home. */
	FString XdgFolder(const TCHAR* Variable, const TCHAR* Default)
	{
		const FString Folder = FPlatformMisc::GetEnvironmentVariable(Variable);
		return Folder.IsEmpty() ? FPaths::Combine(FPlatformProcess::UserHomeDir(), Default) : Folder;
	}
}

FString PlayerDataFolder()
{
#if PLATFORM_ANDROID
	// std::filesystem readers need a physical filename, not an Unreal pak path or content:// URI.
	// Context.getFilesDir() is private to the app and survives application updates.
	static const FString Folder = []()
	{
		JNIEnv* Env = FAndroidApplication::GetJavaEnv();
		jobject Activity = FAndroidApplication::GetGameActivityThis();
		checkf(Env && Activity, TEXT("Android application context is unavailable"));
		jclass ActivityClass = Env->GetObjectClass(Activity);
		jmethodID GetFiles = Env->GetMethodID(ActivityClass, "getFilesDir", "()Ljava/io/File;");
		jobject Files = Env->CallObjectMethod(Activity, GetFiles);
		jclass FileClass = Env->GetObjectClass(Files);
		jmethodID GetPath = Env->GetMethodID(FileClass, "getAbsolutePath", "()Ljava/lang/String;");
		jstring Path = static_cast<jstring>(Env->CallObjectMethod(Files, GetPath));
		const char* Utf8 = Env->GetStringUTFChars(Path, nullptr);
		const FString Result = FPaths::Combine(UTF8_TO_TCHAR(Utf8), TEXT("Weltall"));
		Env->ReleaseStringUTFChars(Path, Utf8);
		Env->DeleteLocalRef(Path);
		Env->DeleteLocalRef(FileClass);
		Env->DeleteLocalRef(Files);
		Env->DeleteLocalRef(ActivityClass);
		return Result;
	}();
	return Folder;
#else
	return PlayerSettingsFolder();
#endif
}

FString PlayerSettingsFolder()
{
#if PLATFORM_ANDROID
	return FPaths::Combine(PlayerDataFolder(), TEXT("Settings"));
#elif PLATFORM_WINDOWS
	return FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("Weltall"));
#elif PLATFORM_MAC
	return FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT("Library/Application Support/Weltall"));
#else
	return FPaths::Combine(XdgFolder(TEXT("XDG_CONFIG_HOME"), TEXT(".config")), TEXT("Weltall"));
#endif
}

FString PlayerSavesFolder()
{
#if PLATFORM_ANDROID
	return FPaths::Combine(PlayerDataFolder(), TEXT("Saves"));
#elif PLATFORM_WINDOWS || PLATFORM_MAC
	return FPaths::Combine(PlayerSettingsFolder(), TEXT("Saves"));
#else
	return FPaths::Combine(XdgFolder(TEXT("XDG_DATA_HOME"), TEXT(".local/share")), TEXT("Weltall"), TEXT("Saves"));
#endif
}
