#include "Shell/AndroidDiagnostics.h"

#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Shell/GameVersion.h"

bool ExportAndroidDiagnostics()
{
	JNIEnv* Env = FAndroidApplication::GetJavaEnv();
	jobject Activity = FAndroidApplication::GetGameActivityThis();
	if (!Env || !Activity) return false;
	jclass Class = Env->GetObjectClass(Activity);
	jmethodID Method = Class ? Env->GetMethodID(Class, "AndroidThunkJava_WeltallExportDiagnostics", "(Ljava/lang/String;Ljava/lang/String;)Z") : nullptr;
	if (Env->ExceptionCheck()) { Env->ExceptionClear(); if (Class) Env->DeleteLocalRef(Class); return false; }
	if (!Method) { if (Class) Env->DeleteLocalRef(Class); return false; }
	const FString Log = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*(FPaths::ProjectLogDir() / TEXT("Weltall.log")));
	jstring Version = Env->NewStringUTF(TCHAR_TO_UTF8(*GameVersion()));
	if (!Version || Env->ExceptionCheck())
	{
		if (Env->ExceptionCheck()) Env->ExceptionClear();
		if (Version) Env->DeleteLocalRef(Version);
		Env->DeleteLocalRef(Class);
		return false;
	}
	jstring LogPath = Env->NewStringUTF(TCHAR_TO_UTF8(*Log));
	if (!LogPath || Env->ExceptionCheck())
	{
		if (Env->ExceptionCheck()) Env->ExceptionClear();
		if (LogPath) Env->DeleteLocalRef(LogPath);
		Env->DeleteLocalRef(Version);
		Env->DeleteLocalRef(Class);
		return false;
	}
	const bool Started = Version && LogPath && Env->CallBooleanMethod(Activity, Method, Version, LogPath) == JNI_TRUE;
	const bool Failed = Env->ExceptionCheck();
	if (Failed) Env->ExceptionClear();
	if (Version) Env->DeleteLocalRef(Version);
	if (LogPath) Env->DeleteLocalRef(LogPath);
	Env->DeleteLocalRef(Class);
	return Started && !Failed;
}
#endif
