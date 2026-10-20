#include "CoreMinimal.h"

#if PLATFORM_ANDROID && WITH_DEV_AUTOMATION_TESTS

#include "Android/AndroidApplication.h"
#include "Audio/GameSoundSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Shell/GameWindowSubsystem.h"
#include "Screens/GameScreenSubsystem.h"
#include "Screens/GameScreen.h"

/** Development-only access to the real tick/audio path; no test API is exposed in Shipping. */
class FWeltallAudioFocusTestAccess
{
public:
	static TSharedPtr<IGameScreen> Screen(UGameScreenSubsystem& Screens) { return Screens.Screen; }
	static bool HasOutput(UGameSoundSubsystem& Sound) { return Sound.Output != nullptr; }
	static bool AudioPaused(UGameSoundSubsystem& Sound) { return Sound.bHeld && Sound.Output && Sound.Output->bIsPaused; }
	static bool AudioPlaying(UGameSoundSubsystem& Sound) { return !Sound.bHeld && Sound.Output && !Sound.Output->bIsPaused; }
};

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

		int32 Request(int32 Gain)
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
			// STREAM_MUSIC=3; gain1=permanent,2=transient,3=duck. This client generates no sound.
			const jint Result = Env->CallIntMethod(Manager, Ask, static_cast<jobject>(nullptr), 3, Gain);
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

	class FFocusCountingScreen final : public IGameScreen
	{
	public:
		explicit FFocusCountingScreen(TSharedRef<IGameScreen> InPrevious) : Previous(MoveTemp(InPrevious)) {}
		void Update(double Delta, Weltall::FPadState) override { ++Updates; Elapsed += Delta; }
		TSharedRef<const IScreenFrame> CaptureFrame() const override { return Previous->CaptureFrame(); }
		TSharedRef<IGameScreen> Previous;
		uint64 Updates = 0;
		double Elapsed = 0.0;
	};

	class FAudioFocusInterruptionCommand final : public IAutomationLatentCommand
	{
	public:
		FAudioFocusInterruptionCommand(FAutomationTestBase& InTest, int32 InGain) : Test(InTest), Gain(InGain) {}
		~FAudioFocusInterruptionCommand()
		{
			Probe.Release();
			if (Window.IsValid()) Window->ResumeAudioIfNeeded();
			if (Screens.IsValid() && Counter) Screens->Show(Counter->Previous);
		}
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
						if (Game)
						{
							Window = Game->GetSubsystem<UGameWindowSubsystem>();
							Screens = Game->GetSubsystem<UGameScreenSubsystem>();
							Sound = Game->GetSubsystem<UGameSoundSubsystem>();
						}
						if (Window.IsValid()) break;
					}
				}
				if (!Window.IsValid() || Window->IsHeldInBackground() || !Screens.IsValid() || !Sound.IsValid()
					|| !FWeltallAudioFocusTestAccess::Screen(*Screens).IsValid() || !FWeltallAudioFocusTestAccess::HasOutput(*Sound))
				{
					if (Now - Started < 15.0) return false;
					Test.AddError(TEXT("The foreground game window was not ready for an OS focus test"));
					return true;
				}
				Counter = MakeShared<FFocusCountingScreen>(FWeltallAudioFocusTestAccess::Screen(*Screens).ToSharedRef());
				Screens->Show(Counter.ToSharedRef());
				Started = Now;
				Phase = 1;
				return false;
			}
			if (!Window.IsValid() || !Sound.IsValid() || !Screens.IsValid())
			{
				Test.AddError(TEXT("The game window disappeared during the focus test"));
				return true;
			}
			if (Phase == 1)
			{
				if (Now - Started < 0.25) return false;
				Test.TestTrue(TEXT("The real screen subsystem advances the generated counter before interruption"), Counter->Updates > 0);
				const int32 Granted = Probe.Request(Gain);
				Test.AddInfo(FString::Printf(TEXT("Actual foreground-process OS focus gain %d request returned %d (1 means granted)"), Gain, Granted));
				if (!Test.TestEqual(TEXT("Android grants the separate focus client"), Granted, 1)) return true;
				Started = Now;
				Phase = 2;
				return false;
			}
			if (Phase == 2)
			{
				if (Now - Started < 0.5) return false;
				Test.TestTrue(TEXT("The game holds simulation during the granted OS interruption"), Window->IsHeldInBackground());
				Test.TestTrue(TEXT("The actual audio component is paused"), FWeltallAudioFocusTestAccess::AudioPaused(*Sound));
				Test.TestEqual(TEXT("Only permanent loss offers explicit reclaim"), Window->CanResumeAudio(), Gain == 1);
				if (Gain != 1) Test.TestFalse(TEXT("User input cannot steal transient focus back"), Window->ResumeAudioIfNeeded());
				HeldUpdates = Counter->Updates;
				HeldElapsed = Counter->Elapsed;
				Phase = 3;
				return false;
			}
			if (Phase == 3)
			{
				if (Now - Started < 2.0) return false;
				Test.TestEqual(TEXT("No game screen updates while OS focus is held elsewhere"), Counter->Updates, HeldUpdates);
				Test.TestEqual(TEXT("No simulated time passes during the interruption"), Counter->Elapsed, HeldElapsed);
				Probe.Release();
				Started = Now;
				Phase = 4;
				return false;
			}
			if (Phase == 4 && Gain == 1)
			{
				if (Now - Started < 0.5) return false;
				Test.TestTrue(TEXT("Permanent loss stays paused until explicit user intent"), Window->IsHeldInBackground());
				Test.TestEqual(TEXT("Permanent loss does not silently advance the game"), Counter->Updates, HeldUpdates);
				Test.TestTrue(TEXT("An explicit resume action requests focus again"), Window->ResumeAudioIfNeeded());
				Started = Now;
				Phase = 5;
				return false;
			}
			if (Now - Started < 0.6) return false;
			Test.TestFalse(TEXT("The game resumes after focus is restored"), Window->IsHeldInBackground());
			Test.TestTrue(TEXT("Screen updates resume"), Counter->Updates > HeldUpdates);
			Test.TestTrue(TEXT("The actual audio component resumes"), FWeltallAudioFocusTestAccess::AudioPlaying(*Sound));
			Test.TestTrue(TEXT("Resume does not simulate time spent interrupted"), Counter->Elapsed - HeldElapsed <= Now - Started + 0.15);
			return true;
		}
	private:
		FAutomationTestBase& Test;
		FTransientFocusProbe Probe;
		TWeakObjectPtr<UGameWindowSubsystem> Window;
		TWeakObjectPtr<UGameScreenSubsystem> Screens;
		TWeakObjectPtr<UGameSoundSubsystem> Sound;
		TSharedPtr<FFocusCountingScreen> Counter;
		uint64 HeldUpdates = 0;
		double HeldElapsed = 0.0;
		double Started = FPlatformTime::Seconds();
		int32 Phase = 0;
		int32 Gain = 2;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidAudioFocusTest, "Weltall.Android.AudioFocus.TransientInterruption",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FAndroidAudioFocusTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FAudioFocusInterruptionCommand>(*this, 2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidAudioDuckingTest, "Weltall.Android.AudioFocus.DuckingInterruption",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FAndroidAudioDuckingTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FAudioFocusInterruptionCommand>(*this, 3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidAudioPermanentTest, "Weltall.Android.AudioFocus.PermanentLossAndExplicitResume",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FAndroidAudioPermanentTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FAudioFocusInterruptionCommand>(*this, 1));
	return true;
}

#endif
