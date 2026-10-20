#include "CoreMinimal.h"

#if PLATFORM_ANDROID && WITH_DEV_AUTOMATION_TESTS

#include "Android/AndroidApplication.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Shell/GameWindowSubsystem.h"

namespace
{
	/** A second, silent OS client in the foreground game process; never broadcast fake focus callbacks. */
	class FTransientFocusProbe
	{
	public:
		~FTransientFocusProbe()
		{
			Release();
			if (Manager)
			{
				FAndroidApplication::GetJavaEnv()->DeleteGlobalRef(Manager);
			}
		}

		int32 Request()
		{
			JNIEnv* Env = FAndroidApplication::GetJavaEnv();
			jobject Activity = FAndroidApplication::GetGameActivityThis();
			if (!Env || !Activity) return -1;
			jclass ActivityClass = Env->GetObjectClass(Activity);
			jmethodID Service = Env->GetMethodID(ActivityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
			jstring Name = Env->NewStringUTF("audio");
			jobject Local = Service ? Env->CallObjectMethod(Activity, Service, Name) : nullptr;
			Env->DeleteLocalRef(Name);
			Env->DeleteLocalRef(ActivityClass);
			if (Env->ExceptionCheck() || !Local)
			{
				Env->ExceptionClear();
				return -1;
			}
			Manager = Env->NewGlobalRef(Local);
			jclass ManagerClass = Env->GetObjectClass(Local);
			Env->DeleteLocalRef(Local);
			jmethodID Ask = Env->GetMethodID(ManagerClass, "requestAudioFocus", "(Landroid/media/AudioManager$OnAudioFocusChangeListener;II)I");
			Abandon = Env->GetMethodID(ManagerClass, "abandonAudioFocus", "(Landroid/media/AudioManager$OnAudioFocusChangeListener;)I");
			Env->DeleteLocalRef(ManagerClass);
			if (Env->ExceptionCheck() || !Ask || !Abandon)
			{
				Env->ExceptionClear();
				return -1;
			}
			// The legacy null-listener client has a different OS focus identity from the game's listener.
			// STREAM_MUSIC=3, AUDIOFOCUS_GAIN_TRANSIENT=2; this client generates no sound.
			const jint Result = Env->CallIntMethod(Manager, Ask, static_cast<jobject>(nullptr), 3, 2);
			if (Env->ExceptionCheck())
			{
				Env->ExceptionClear();
				return -1;
			}
			return Result;
		}

		void Release()
		{
			if (Manager && Abandon)
			{
				JNIEnv* Env = FAndroidApplication::GetJavaEnv();
				Env->CallIntMethod(Manager, Abandon, static_cast<jobject>(nullptr));
				if (Env->ExceptionCheck()) Env->ExceptionClear();
				Abandon = nullptr;
			}
		}
	private:
		jobject Manager = nullptr;
		jmethodID Abandon = nullptr;
	};

	class FAudioFocusInterruptionCommand final : public IAutomationLatentCommand
	{
	public:
		explicit FAudioFocusInterruptionCommand(FAutomationTestBase& InTest) : Test(InTest) {}
		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Phase == 0)
			{
				if (GEngine)
				{
					for (const FWorldContext& Context : GEngine->GetWorldContexts())
					{
						UWorld* World = Context.World();
						UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
						if (Game) Window = Game->GetSubsystem<UGameWindowSubsystem>();
						if (Window.IsValid()) break;
					}
				}
				if (!Window.IsValid() || Window->IsHeldInBackground())
				{
					if (Now - Started < 15.0) return false;
					Test.AddError(TEXT("The foreground game window was not ready for an OS focus test"));
					return true;
				}
				const int32 Granted = Probe.Request();
				Test.AddInfo(FString::Printf(TEXT("Actual foreground-process OS transient focus request returned %d (1 means granted)"), Granted));
				if (!Test.TestEqual(TEXT("Android grants the separate transient focus client"), Granted, 1)) return true;
				Started = Now;
				Phase = 1;
				return false;
			}
			if (!Window.IsValid())
			{
				Test.AddError(TEXT("The game window disappeared during the focus test"));
				return true;
			}
			if (Phase == 1)
			{
				if (Now - Started < 2.0) return false;
				Test.TestTrue(TEXT("The game holds simulation during the granted OS interruption"), Window->IsHeldInBackground());
				Probe.Release();
				Started = Now;
				Phase = 2;
				return false;
			}
			if (Now - Started < 1.0) return false;
			Test.TestFalse(TEXT("The game resumes after the OS client abandons transient focus"), Window->IsHeldInBackground());
			return true;
		}
	private:
		FAutomationTestBase& Test;
		FTransientFocusProbe Probe;
		TWeakObjectPtr<UGameWindowSubsystem> Window;
		double Started = FPlatformTime::Seconds();
		int32 Phase = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidAudioFocusTest, "Weltall.Android.AudioFocus.TransientInterruption",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FAndroidAudioFocusTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FAudioFocusInterruptionCommand>(*this));
	return true;
}

#endif
