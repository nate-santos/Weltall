#include "CoreMinimal.h"

#if PLATFORM_ANDROID && WITH_DEV_AUTOMATION_TESTS

#include "Android/AndroidApplication.h"
#include "Algo/AllOf.h"
#include "Audio/GameSoundSubsystem.h"
#include "Audio/GameSoundWave.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "Input/TouchControls/SWeltallTouchControls.h"
#include "Input/WeltallPlayerController.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Shell/AndroidAudioFocus.h"
#include "Shell/PlayerFolders.h"
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
	static uint64 GeneratedSamples(UGameSoundSubsystem& Sound)
	{
		const UGameSoundWave* Wave = Sound.Output ? Cast<UGameSoundWave>(Sound.Output->Sound) : nullptr;
		return Wave ? Wave->GeneratedSamples.load(std::memory_order_relaxed) : 0;
	}
	static TSharedPtr<SWeltallTouchControls> Touch(AWeltallPlayerController& Controller) { return Controller.TouchControls; }
	static Weltall::FTouchPoint ControlPoint(SWeltallTouchControls& Touch, Weltall::ETouchControl Control)
	{
		const Weltall::FTouchRect& Rect = Touch.Layout.Controls[static_cast<std::size_t>(Control)];
		Weltall::FTouchPoint Point = Rect.Center();
		if (Control == Weltall::ETouchControl::DPad) Point.Y += Rect.Height * 0.3f;
		return Point;
	}
	static Weltall::FTouchPoint ResumePoint(SWeltallTouchControls& Touch) { return Touch.AudioPausePanel().Center(); }
	static bool Pointer(SWeltallTouchControls& Touch, Weltall::FTouchPoint Point, bool bPressed)
	{
		const FGeometry& Geometry = Touch.GetCachedGeometry();
		const FVector2D Absolute = Geometry.LocalToAbsolute(FVector2D(Point.X, Point.Y));
		const FPointerEvent Event(FInputDeviceId::CreateFromInternalId(0), 77, Absolute, Absolute, bPressed ? 1.0f : 0.0f, bPressed);
		return (bPressed ? Touch.OnTouchStarted(Geometry, Event) : Touch.OnTouchEnded(Geometry, Event)).IsEventHandled();
	}
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
		void Update(double Delta, Weltall::FPadState Pad) override { ++Updates; Elapsed += Delta; SeenButtons |= Pad.Buttons; }
		TSharedRef<const IScreenFrame> CaptureFrame() const override { return Previous->CaptureFrame(); }
		TSharedRef<IGameScreen> Previous;
		uint64 Updates = 0;
		double Elapsed = 0.0;
		uint16 SeenButtons = 0;
	};

	enum class EResumeInput { Native, Panel, Control, Hardware };

	/** Separately named because a host must press Home and return after the actual policy result. */
	class FAudioPolicyCommand final : public IAutomationLatentCommand
	{
	public:
		explicit FAudioPolicyCommand(FAutomationTestBase& InTest, bool bInDelayed = false) : Test(InTest), bDelayed(bInDelayed) {}
		~FAudioPolicyCommand()
		{
			if (Window.IsValid()) Window->ResumeAudioIfNeeded();
			if (Controller.IsValid()) Controller->ResetAllInput();
			if (Screens.IsValid() && Counter) Screens->Show(Counter->Previous);
		}
		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 75.0)
			{
				Test.AddError(TEXT("Host-driven audio policy hold/resume did not finish within 75 seconds"));
				return true;
			}
			if (Phase == 0)
			{
				FParse::Value(FCommandLine::Get(), TEXT("WeltallAudioPolicy="), Run);
				if (Run.IsEmpty() || Run.Len() > 64 || !Algo::AllOf(Run, [](TCHAR C) { return FChar::IsAlnum(C) || C == '-' || C == '_'; }))
				{
					Test.AddError(TEXT("Use the host audio-policy runner with a unique -WeltallAudioPolicy identifier"));
					return true;
				}
				if (GEngine) for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					UWorld* World = Context.World();
					UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
					if (!Game) continue;
					Window = Game->GetSubsystem<UGameWindowSubsystem>();
					Screens = Game->GetSubsystem<UGameScreenSubsystem>();
					Sound = Game->GetSubsystem<UGameSoundSubsystem>();
					Controller = Cast<AWeltallPlayerController>(Game->GetFirstLocalPlayerController());
					if (Window.IsValid()) break;
				}
				if (Controller.IsValid()) Touch = FWeltallAudioFocusTestAccess::Touch(*Controller);
				if (!Window.IsValid() || Window->IsHeldInBackground() || !Screens.IsValid() || !Sound.IsValid()
					|| !Controller.IsValid() || !Touch || Touch->GetCachedGeometry().GetLocalSize().X < 100.0f
					|| !FWeltallAudioFocusTestAccess::HasOutput(*Sound) || !FWeltallAudioFocusTestAccess::Screen(*Screens)) return false;
				Root = FPaths::Combine(PlayerDataFolder(), TEXT("Testing/AudioPolicy"), Run);
				if (IFileManager::Get().DirectoryExists(*Root) || !IFileManager::Get().MakeDirectory(*Root, true))
				{
					Test.AddError(TEXT("Audio-policy identifier must name a new private test directory"));
					return true;
				}
				Counter = MakeShared<FFocusCountingScreen>(FWeltallAudioFocusTestAccess::Screen(*Screens).ToSharedRef());
				Screens->Show(Counter.ToSharedRef());
				Controller->ResetAllInput();
				const auto Down = FWeltallAudioFocusTestAccess::ControlPoint(*Touch, Weltall::ETouchControl::DPad);
				Test.TestTrue(TEXT("Touch Down is held before background policy denial"), FWeltallAudioFocusTestAccess::Pointer(*Touch, Down, true));
				if (!bDelayed && !ScheduleProbe("AndroidThunkJava_WeltallProbeDeniedAudio")) return true;
				if (!FFileHelper::SaveStringToFile(TEXT("Press Home; return after result.txt appears.\n"), *FPaths::Combine(Root, TEXT("ready.txt"))))
				{
					Test.AddError(TEXT("Cannot notify host to background the game")); return true;
				}
				Phase = 1;
				return false;
			}
			if (!Window.IsValid() || !Screens.IsValid() || !Sound.IsValid() || !Controller.IsValid())
			{
				Test.AddError(TEXT("Game subsystems disappeared during denied-focus test")); return true;
			}
			if (Phase == 1)
			{
				if (bDelayed && !bProbeScheduled && Window->IsHeldForAudio() && ReadAndroidAudioFocus().State == EWeltallAudioFocus::Waiting)
				{
					if (!ScheduleProbe("AndroidThunkJava_WeltallProbeDelayedAudio")) return true;
					bProbeScheduled = true;
				}
				FString Result;
				if (!IFileManager::Get().FileExists(*FPaths::Combine(Root, TEXT("result.txt")))) return false;
				if (!FFileHelper::LoadFileToString(Result, *FPaths::Combine(Root, TEXT("result.txt")))) return false;
				if (Window->IsHeldInBackground() && !Window->IsHeldForAudio()) return false;
				Test.AddInfo(TEXT("Actual background policy request: ") + Result);
				if (!Test.TestTrue(TEXT("Android returns the actual expected policy result"), Result.Contains(TEXT("eligible=true\n"))
					&& Result.Contains(bDelayed ? TEXT("state=4\n") : TEXT("state=5\n"))
					&& Result.Contains(bDelayed ? TEXT("request_result=2\n") : TEXT("request_result=0\n"))
					&& ReadAndroidAudioFocus().State == (bDelayed ? EWeltallAudioFocus::Waiting : EWeltallAudioFocus::Denied))) return true;
				Test.TestEqual(TEXT("Only denied focus offers explicit resume; waiting does not steal focus"), Window->CanResumeAudio(), !bDelayed);
				Test.TestTrue(TEXT("Denied focus pauses the actual audio component"), FWeltallAudioFocusTestAccess::AudioPaused(*Sound));
				HeldSamples = FWeltallAudioFocusTestAccess::GeneratedSamples(*Sound);
				Test.TestEqual(TEXT("Background/denial clears previously held input"), Controller->HeldPad().Buttons, static_cast<uint16>(0));
				Counter->SeenButtons = 0; HeldUpdates = Counter->Updates; HeldElapsed = Counter->Elapsed;
				PhaseAt = Now; Phase = 2; return false;
			}
			if (Phase == 2)
			{
				if (Now - PhaseAt < 1.0) return false;
				Test.TestEqual(TEXT("No simulation updates while foreground focus remains denied"), Counter->Updates, HeldUpdates);
				Test.TestEqual(TEXT("No simulated time advances while denied"), Counter->Elapsed, HeldElapsed);
				Test.TestEqual(TEXT("No new PCM is requested from the paused game wave"), FWeltallAudioFocusTestAccess::GeneratedSamples(*Sound), HeldSamples);
				if (bDelayed)
				{
					Test.TestFalse(TEXT("User intent cannot steal the locked focus"), Window->ResumeAudioIfNeeded());
					const auto Point = FWeltallAudioFocusTestAccess::ControlPoint(*Touch, Weltall::ETouchControl::Cross);
					Test.TestTrue(TEXT("The real widget consumes a touch while focus is delayed"), FWeltallAudioFocusTestAccess::Pointer(*Touch, Point, true));
					FWeltallAudioFocusTestAccess::Pointer(*Touch, Point, false);
					if (!FFileHelper::SaveStringToFile(TEXT("Release own focus lock.\n"), *FPaths::Combine(Root, TEXT("release-ready.txt"))))
					{
						Test.AddError(TEXT("Cannot signal host to release its own focus lock")); return true;
					}
					Phase = 5; PhaseAt = Now; return false;
				}
				const auto Point = FWeltallAudioFocusTestAccess::ResumePoint(*Touch);
				Test.TestTrue(TEXT("Real resume panel accepts explicit focus retry"), FWeltallAudioFocusTestAccess::Pointer(*Touch, Point, true));
				FWeltallAudioFocusTestAccess::Pointer(*Touch, Point, false);
				RetryAt = PhaseAt = Now; Phase = 3; return false;
			}
			if (Phase == 5)
			{
				if (Window->IsHeldInBackground() || ReadAndroidAudioFocus().State != EWeltallAudioFocus::Granted) return false;
				Test.AddInfo(TEXT("The real delayed request gains focus automatically after the host lock is released"));
				RetryAt = PhaseAt = Now; Phase = 3; return false;
			}
			if (Now - PhaseAt < 0.75) return false;
			if (Phase == 3)
			{
				ResumedSamples = FWeltallAudioFocusTestAccess::GeneratedSamples(*Sound);
				Test.TestTrue(TEXT("Real audio render callbacks restart after foreground retry"), ResumedSamples > HeldSamples);
				Phase = 4; PhaseAt = Now; return false;
			}
			Test.TestTrue(TEXT("Android grants the foreground retry"), ReadAndroidAudioFocus().State == EWeltallAudioFocus::Granted);
			Test.TestFalse(TEXT("Denied-focus recovery releases the simulation hold"), Window->IsHeldInBackground());
			Test.TestTrue(TEXT("Real screen updates and audio resume"), Counter->Updates > HeldUpdates && FWeltallAudioFocusTestAccess::AudioPlaying(*Sound));
			Test.TestTrue(TEXT("PCM callbacks continue beyond the initial queued buffers"), FWeltallAudioFocusTestAccess::GeneratedSamples(*Sound) > ResumedSamples);
			Test.AddInfo(FString::Printf(TEXT("Actual PCM samples: held=%llu, first resumed window=%llu, second=%llu"),
				static_cast<unsigned long long>(HeldSamples), static_cast<unsigned long long>(ResumedSamples),
				static_cast<unsigned long long>(FWeltallAudioFocusTestAccess::GeneratedSamples(*Sound))));
			Test.TestEqual(TEXT("Neither stale input nor resume tap reaches gameplay"), Counter->SeenButtons, static_cast<uint16>(0));
			Test.TestTrue(TEXT("Retry discards interrupted elapsed time"), Counter->Elapsed - HeldElapsed <= Now - RetryAt + 0.15);
			return true;
		}
	private:
		bool ScheduleProbe(const char* Name)
		{
			JNIEnv* Env = FAndroidApplication::GetJavaEnv();
			jobject Activity = FAndroidApplication::GetGameActivityThis();
			jclass Class = Env->GetObjectClass(Activity);
			jmethodID Method = Env->GetMethodID(Class, Name, "(Ljava/lang/String;)V");
			if (Env->ExceptionCheck() || !Method)
			{
				Env->ExceptionClear(); Env->DeleteLocalRef(Class);
				Test.AddError(TEXT("Development Java policy probe is unavailable")); return false;
			}
			jstring Id = Env->NewStringUTF(TCHAR_TO_UTF8(*Run));
			Env->CallVoidMethod(Activity, Method, Id);
			Env->DeleteLocalRef(Id); Env->DeleteLocalRef(Class);
			if (!Env->ExceptionCheck()) return true;
			Env->ExceptionClear(); Test.AddError(TEXT("Cannot schedule actual audio policy request")); return false;
		}
		FAutomationTestBase& Test;
		TWeakObjectPtr<UGameWindowSubsystem> Window;
		TWeakObjectPtr<UGameScreenSubsystem> Screens;
		TWeakObjectPtr<UGameSoundSubsystem> Sound;
		TWeakObjectPtr<AWeltallPlayerController> Controller;
		TSharedPtr<SWeltallTouchControls> Touch;
		TSharedPtr<FFocusCountingScreen> Counter;
		FString Root, Run;
		double Started = FPlatformTime::Seconds(), PhaseAt = 0, RetryAt = 0, HeldElapsed = 0;
		uint64 HeldUpdates = 0;
		uint64 HeldSamples = 0, ResumedSamples = 0;
		int32 Phase = 0;
		bool bDelayed = false, bProbeScheduled = false;
	};

	class FAudioFocusInterruptionCommand final : public IAutomationLatentCommand
	{
	public:
		FAudioFocusInterruptionCommand(FAutomationTestBase& InTest, int32 InGain, EResumeInput InResume = EResumeInput::Native)
			: Test(InTest), Gain(InGain), Resume(InResume) {}
		~FAudioFocusInterruptionCommand()
		{
			Probe.Release();
			if (Window.IsValid()) Window->ResumeAudioIfNeeded();
			if (Touch) FWeltallAudioFocusTestAccess::Pointer(*Touch, HeldPoint, false);
			if (Controller.IsValid()) Controller->ResetAllInput();
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
							Controller = Cast<AWeltallPlayerController>(Game->GetFirstLocalPlayerController());
						}
						if (Window.IsValid()) break;
					}
				}
				if (Controller.IsValid()) Touch = FWeltallAudioFocusTestAccess::Touch(*Controller);
				if (!Window.IsValid() || Window->IsHeldInBackground() || !Screens.IsValid() || !Sound.IsValid() || !Touch
					|| Touch->GetCachedGeometry().GetLocalSize().X < 100.0f
					|| !FWeltallAudioFocusTestAccess::Screen(*Screens).IsValid() || !FWeltallAudioFocusTestAccess::HasOutput(*Sound))
				{
					if (Now - Started < 15.0) return false;
					Test.AddError(TEXT("The foreground game window was not ready for an OS focus test"));
					return true;
				}
				Counter = MakeShared<FFocusCountingScreen>(FWeltallAudioFocusTestAccess::Screen(*Screens).ToSharedRef());
				Screens->Show(Counter.ToSharedRef());
				Controller->ResetAllInput();
				HeldPoint = FWeltallAudioFocusTestAccess::ControlPoint(*Touch, Weltall::ETouchControl::DPad);
				Test.TestTrue(TEXT("The real touch widget accepts a held Down before interruption"), FWeltallAudioFocusTestAccess::Pointer(*Touch, HeldPoint, true));
				Started = Now;
				Phase = 1;
				return false;
			}
			if (!Window.IsValid() || !Sound.IsValid() || !Screens.IsValid() || !Controller.IsValid())
			{
				Test.AddError(TEXT("The game window disappeared during the focus test"));
				return true;
			}
			if (Phase == 1)
			{
				if (Now - Started < 0.25) return false;
				Test.TestTrue(TEXT("The real screen subsystem advances the generated counter before interruption"), Counter->Updates > 0);
				Test.TestTrue(TEXT("The held Down reaches the real screen update path"), (Counter->SeenButtons & Weltall::FPadState::Bit(Weltall::EPadButton::Down)) != 0);
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
				Test.TestEqual(TEXT("Focus loss clears the held touch through the real controller"), Controller->HeldPad().Buttons, static_cast<uint16>(0));
				Test.TestEqual(TEXT("Only permanent loss offers explicit reclaim"), Window->CanResumeAudio(), Gain == 1);
				if (Gain != 1) Test.TestFalse(TEXT("User input cannot steal transient focus back"), Window->ResumeAudioIfNeeded());
				if (Gain != 1)
				{
					// A quick touch during the hold must neither steal focus nor become a delayed game action.
					const Weltall::FTouchPoint Cross = FWeltallAudioFocusTestAccess::ControlPoint(*Touch, Weltall::ETouchControl::Cross);
					Test.TestTrue(TEXT("The actual widget receives a touch during transient loss"), FWeltallAudioFocusTestAccess::Pointer(*Touch, Cross, true));
					FWeltallAudioFocusTestAccess::Pointer(*Touch, Cross, false);
					Test.TestTrue(TEXT("A touch does not steal transient OS focus"), Window->IsHeldInBackground());
				}
				Counter->SeenButtons = 0;
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
				bool bHandled = false;
				if (Resume == EResumeInput::Native) bHandled = Window->ResumeAudioIfNeeded();
				else if (Resume == EResumeInput::Hardware)
				{
					const FInputKeyEventArgs Press(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::Gamepad_FaceButton_Bottom, IE_Pressed, 1.0f, false, FPlatformTime::Cycles64());
					bHandled = Controller->InputKey(Press);
				}
				else
				{
					const Weltall::FTouchPoint Point = Resume == EResumeInput::Panel ? FWeltallAudioFocusTestAccess::ResumePoint(*Touch)
						: FWeltallAudioFocusTestAccess::ControlPoint(*Touch, Weltall::ETouchControl::Cross);
					bHandled = FWeltallAudioFocusTestAccess::Pointer(*Touch, Point, true);
					FWeltallAudioFocusTestAccess::Pointer(*Touch, Point, false);
				}
				Test.TestTrue(TEXT("The explicit resume input is handled by its production entry point"), bHandled);
				Test.TestEqual(TEXT("The resume input is consumed instead of becoming a game action"), Controller->HeldPad().Buttons, static_cast<uint16>(0));
				Started = Now;
				Phase = 5;
				return false;
			}
			if (Now - Started < 0.6) return false;
			Test.TestFalse(TEXT("The game resumes after focus is restored"), Window->IsHeldInBackground());
			Test.TestTrue(TEXT("Screen updates resume"), Counter->Updates > HeldUpdates);
			Test.TestTrue(TEXT("The actual audio component resumes"), FWeltallAudioFocusTestAccess::AudioPlaying(*Sound));
			Test.TestEqual(TEXT("No stale held or queued button reaches gameplay after regain"), Counter->SeenButtons, static_cast<uint16>(0));
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
		TWeakObjectPtr<AWeltallPlayerController> Controller;
		TSharedPtr<SWeltallTouchControls> Touch;
		Weltall::FTouchPoint HeldPoint;
		uint64 HeldUpdates = 0;
		double HeldElapsed = 0.0;
		double Started = FPlatformTime::Seconds();
		int32 Phase = 0;
		int32 Gain = 2;
		EResumeInput Resume = EResumeInput::Native;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidAudioPanelResumeTest, "Weltall.Android.AudioFocus.PermanentLossPanelResume",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FAndroidAudioPanelResumeTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FAudioFocusInterruptionCommand>(*this, 1, EResumeInput::Panel));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidAudioControlResumeTest, "Weltall.Android.AudioFocus.PermanentLossTouchControlResume",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FAndroidAudioControlResumeTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FAudioFocusInterruptionCommand>(*this, 1, EResumeInput::Control));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidAudioHardwareResumeTest, "Weltall.Android.AudioFocus.PermanentLossControllerHandlerResume",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FAndroidAudioHardwareResumeTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FAudioFocusInterruptionCommand>(*this, 1, EResumeInput::Hardware));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidDeniedAudioPolicyTest, "Weltall.Android.HostDrivenAudioPolicy.BackgroundDenialAndPanelRetry",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FAndroidDeniedAudioPolicyTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FAudioPolicyCommand>(*this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidDelayedAudioPolicyTest, "Weltall.Android.HostDrivenAudioPolicy.LockedDelayedGrant",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FAndroidDelayedAudioPolicyTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FAudioPolicyCommand>(*this, true));
	return true;
}

#endif
