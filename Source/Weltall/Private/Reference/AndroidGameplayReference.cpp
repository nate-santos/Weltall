#include "CoreMinimal.h"

#if PLATFORM_ANDROID && WITH_DEV_AUTOMATION_TESTS

#include "Audio/GameSoundSubsystem.h"
#include "Battle/BattleRuntime.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/BattleGameProgram.h"
#include "Game/FieldMovie.h"
#include "Game/FieldProgram.h"
#include "Game/GameLoop.h"
#include "Game/OriginalSave.h"
#include "Game/SaveStore.h"
#include "Game/WorldMapProgram.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Reference/ReferenceInput.h"
#include "Reference/ReferenceBattleDriver.h"
#include "Reference/RenderRoute.h"
#include "Screens/GameScreenSubsystem.h"
#include "Shell/GameDiscsSubsystem.h"
#include "Shell/GameWindowSubsystem.h"
#include "Shell/PlayerFolders.h"
#include "Shell/PlayerSettingsSubsystem.h"
#include "UnrealClient.h"

#include <filesystem>

DEFINE_LOG_CATEGORY_STATIC(LogGameplayReference, Log, All);

namespace WeltallAndroidReference
{
constexpr double TickSeconds = 1.0 / 60.0;

/** A real screen, fed the PC reference's normal input. Only its save store is isolated. */
class FScriptedScreen final : public IGameScreen
{
public:
	FScriptedScreen(FString InRoot, std::unique_ptr<Weltall::FSaveStore> InStore, TSharedRef<FGameLoopScreen> InGame)
		: Root(MoveTemp(InRoot)), Store(std::move(InStore)), Game(MoveTemp(InGame)) {}

	bool Configure(const FString& Params)
	{
		bProfile = FParse::Param(*Params, TEXT("Profile"));
		if (bProfile && FCsvProfiler::IsCapturing()) return false;
		FParse::Value(*Params, TEXT("Seconds="), Duration);
		FParse::Value(*Params, TEXT("Every="), Every);
		if (!FMath::IsFinite(Duration) || Duration < 40.0 || Duration > 7200.0
			|| !FMath::IsFinite(Every) || (Every != 0.0 && Every < 1.0) || Every > 60.0) return false;
		NextShot = Every;
		// The caller cannot redirect a parser's file access or disable encounters through a route.
		const FString ScriptParams = FString::Printf(TEXT("-PadFile=\"%s\" -PadStep=0.1"), *FPaths::Combine(Root, TEXT("controller.txt")));
		if (!WeltallReference::ParsePadScript(ScriptParams, Script, PadStep) || !Reader.Parse(Params) || !BattleDriver.Parse(Params)
			|| !FRenderRoute::Parse(Params + TEXT(" -RouteBattles"), Route)) return false;
		if (BattleDriver.IsEnabled()) Write(TEXT("BattleDriver ready=StoreCombo"));
		return !bFailed;
	}

	void Update(double Delta, Weltall::FPadState) override
	{
		if (bFinished) return;
		if (bProfile && !bProfileStarted && Elapsed >= 36.0)
		{
			FCsvProfiler::Get()->BeginCapture(-1, Root, TEXT("profile.csv"));
			bProfileStarted = true;
		}
		Owed += Delta;
		// The same 60 Hz input sampling as RenderScreen, retaining real foreground time.
		while (Owed + 1e-9 >= TickSeconds && Elapsed + TickSeconds / 2 < Duration)
		{
			const int32 Index = static_cast<int32>(Elapsed / PadStep);
			WeltallReference::FPadStep Step = Script.IsValidIndex(Index) ? Script[Index] : WeltallReference::FPadStep{};
			Step.Held.Buttons |= Route.Step(*Game, Elapsed).Buttons;
			const Weltall::FBattleGameProgram* Battle = Game->Loop() ? Game->Loop()->Battle() : nullptr;
			Step.Held.Buttons |= BattleDriver.Step(Battle ? &Battle->Runtime().Context() : nullptr, TickSeconds).Buttons;
			if (Reader.bReads && Reader.Holds(*Game, TickSeconds))
			{
				Step.Held.Buttons |= Weltall::FPadState::Bit(Weltall::EPadButton::Cross);
				if (Reader.Held == 0.0) Write(FString::Printf(TEXT("Reader: confirms at %.3f s"), Elapsed));
			}
			Game->AddViewInput(Step.ViewInput(TickSeconds));
			Game->Update(TickSeconds, Step.Held);
			Elapsed += TickSeconds;
			Owed -= TickSeconds;
			if (Elapsed >= NextTrace)
			{
				Trace(Step.Held);
				NextTrace += 2.0;
				Progress();
			}
			if (Game->HasStopped()) { bFailed = bFinished = true; break; }
		}
		if (Elapsed + TickSeconds / 2 >= Duration) bFinished = true;
		if (bFinished && bProfileStarted) ProfileFile = FCsvProfiler::Get()->EndCapture();
		// A profiling control can omit periodic GPU readbacks while retaining the required final image.
		if ((Every > 0.0 && Elapsed >= NextShot) || bFinished)
		{
			const FString Name = bFinished ? TEXT("frame.png") : FString::Printf(TEXT("frame-%06.2f.png"), NextShot);
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(Root, Name), false, false);
			NextShot += Every;
		}
		if (bFinished) { Trace({}); Progress(); }
	}

	TSharedRef<const IScreenFrame> CaptureFrame() const override { return Game->CaptureFrame(); }
	// Physical devices remain on the ordinary input path, but cannot alter a scripted reference.
	// This tool therefore supplies no touch/gamepad acceptance by itself.
	bool Finished() const { return bFinished; }
	bool Failed() const { return bFailed; }
	bool ProfileReady() const { return !bProfile || (ProfileFile.IsValid() && ProfileFile.IsReady()); }
	double Seconds() const { return Elapsed; }
	double Limit() const { return Duration; }
	Weltall::FSaveStore& Saves() { return *Store; }

private:
	void Write(const FString& Line)
	{
		UE_LOG(LogGameplayReference, Display, TEXT("%s"), *Line);
		if (!FFileHelper::SaveStringToFile(Line + TEXT("\n"), *FPaths::Combine(Root, TEXT("trace.log")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append)) bFailed = true;
	}
	void Progress()
	{
		const FString Text = FString::Printf(TEXT("{\"elapsed\":%.3f,\"finished\":%s,\"failed\":%s}\n"), Elapsed,
			bFinished ? TEXT("true") : TEXT("false"), bFailed ? TEXT("true") : TEXT("false"));
		if (!FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Root, TEXT("progress.json")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bFailed = true;
	}
	void Trace(Weltall::FPadState Held)
	{
		const Weltall::FGameLoop* Loop = Game->Loop();
		if (!Loop) return;
		Write(FString::Printf(TEXT("GameTrace %.3f stage=%d program=%d pad=%04X"), Elapsed,
			static_cast<int32>(Loop->Stage()), static_cast<int32>(Loop->Program()), Held.Buttons));
		if (const Weltall::FFieldProgram* Field = Loop->Field())
		{
			const Weltall::FEventContext& Context = Field->Runtime().Context();
			if (Context.LeaderActor < Context.States.size())
			{
				const auto& At = Context.States[Context.LeaderActor].Position;
				const Weltall::FFieldMovie* Movie = Field->Movie();
				Write(FString::Printf(TEXT("GameTrace field map=%d leader=%d position=%d,%d,%d lock=%04X yaw=%d disc=%d movie=%d"),
					Context.RunningMap, Context.LeaderActor, At[0] >> 16, At[1] >> 16, At[2] >> 16, Context.PlayerLock,
					Context.Camera.GoalViewYaw, Context.Disc, Movie ? static_cast<int32>(Movie->Request().Movie) : -1));
				if (Movie) Write(FString::Printf(TEXT("GameTrace field movie picture=%u view=%d"), Movie->Player().PictureNumber(), static_cast<int32>(Movie->View())));
			}
		}
		if (const Weltall::FBattleGameProgram* Battle = Loop->Battle())
		{
			const Weltall::FBattleContext& Context = Battle->Runtime().Context();
			Write(FString::Printf(TEXT("GameTrace battle actor=%d menu=%d end=%d waits=%d,%d,%d"),
				Context.Party.Actor, Context.Party.MenuState, Context.End, Context.Waits[0], Context.Waits[1], Context.Waits[2]));
		}
		if (const Weltall::FWorldMapProgram* World = Loop->WorldMap())
		{
			const Weltall::FWorldMapContext& Context = World->Runtime().Context();
			const auto& Leader = Context.Tasks[Weltall::FWorldMapContext::LeaderSlot].Position;
			const auto& Vehicle = Context.Tasks[Weltall::FWorldMapContext::VehicleSlot].Position;
			Write(FString::Printf(TEXT("GameTrace world travel=%d leader=%d,%d,%d vehicle=%d,%d,%d yaw=%d roll=%d place=%d notice=%d"),
				Context.TravelMode, Leader.X >> 12, Leader.Y >> 12, Leader.Z >> 12, Vehicle.X >> 12, Vehicle.Y >> 12, Vehicle.Z >> 12,
				Context.CameraYaw, Context.CameraRoll, Context.Place, Context.Notice));
		}
	}

	FString Root;
	// Reverse destruction order keeps the store alive until the screen releases it.
	std::unique_ptr<Weltall::FSaveStore> Store;
	TSharedRef<FGameLoopScreen> Game;
	TArray<WeltallReference::FPadStep> Script;
	WeltallReference::FRenderReader Reader;
	WeltallReference::FReferenceBattleDriver BattleDriver;
	FRenderRoute Route;
	double PadStep = 0.1, Duration = 70.0, Every = 20.0;
	double Elapsed = 0.0, Owed = 0.0, NextTrace = 0.0, NextShot = 20.0;
	bool bFinished = false, bFailed = false;
	bool bProfile = false, bProfileStarted = false;
	TSharedFuture<FString> ProfileFile;
};

class FReferenceCommand final : public IAutomationLatentCommand
{
public:
	explicit FReferenceCommand(FAutomationTestBase& InTest) : Test(InTest) {}
	bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (!Screen)
		{
			UGameInstance* Game = nullptr;
			if (GEngine) for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && World->GetGameInstance()) { Game = World->GetGameInstance(); break; }
			}
			UGameScreenSubsystem* Screens = Game ? Game->GetSubsystem<UGameScreenSubsystem>() : nullptr;
			UGameWindowSubsystem* Window = Game ? Game->GetSubsystem<UGameWindowSubsystem>() : nullptr;
			UGameSoundSubsystem* Sound = Game ? Game->GetSubsystem<UGameSoundSubsystem>() : nullptr;
			UGameDiscsSubsystem* Discs = Game ? Game->GetSubsystem<UGameDiscsSubsystem>() : nullptr;
			if (!Screens || !Window || Window->IsHeldInBackground() || !Sound || !Sound->Session() || !Discs
				|| !Screens->CaptureFrame() || !Discs->Library().PlayableDisc(1) || !Discs->Library().PlayableDisc(2))
			{
				if (Now - Started < 30.0) return false;
				Test.AddError(TEXT("Foreground game and both discs were not ready")); return true;
			}
			FString Id;
			FParse::Value(FCommandLine::Get(), TEXT("WeltallReference="), Id);
			bool bValidId = !Id.IsEmpty() && Id.Len() <= 120;
			for (TCHAR C : Id) bValidId &= FChar::IsAlnum(C) || C == '-' || C == '_';
			if (!Test.TestTrue(TEXT("Reference id contains only letters, digits, dash or underscore"),
				bValidId)) return true;
			Root = FPaths::Combine(PlayerDataFolder(), TEXT("Testing/GameplayReference"), Id);
			FString Params;
			if (!Test.TestTrue(TEXT("Reference request and native save can be read"),
				FFileHelper::LoadFileToString(Params, *FPaths::Combine(Root, TEXT("request.txt")))
				&& FFileHelper::LoadFileToArray(Input, *FPaths::Combine(Root, TEXT("input.sav"))))) return true;
			const auto Save = Weltall::FOriginalSave::FromData({Input.GetData(), static_cast<size_t>(Input.Num())});
			if (!Test.TestTrue(TEXT("The unchanged source is a valid native save"), static_cast<bool>(Save))) return true;
			const FString SavesRoot = FPaths::Combine(Root, TEXT("Saves"));
			if (!Test.TestFalse(TEXT("The isolated save store has never been used"), IFileManager::Get().DirectoryExists(*SavesRoot))) return true;
			auto Store = std::make_unique<Weltall::FSaveStore>(std::filesystem::path(TCHAR_TO_UTF8(*SavesRoot)));
			if (!Test.TestFalse(TEXT("A byte-identical copy is placed only in the isolated store"), Store->Copy(0, Save->Data()).has_value())) return true;
			const Weltall::FIso9660* Disc = Discs->Library().PlayableDisc(1);
			Screens->Close(); // Release the previous screen before its sound session is replaced.
			auto Powered = FGameLoopScreen::PowerOn(*Disc, [Sound, Disc]() { return Sound->Start(*Disc) ? Sound->Session() : nullptr; },
				Game->GetSubsystem<UPlayerSettingsSubsystem>()->Settings(), Discs->Library().PlayableDisc(2));
			if (!Test.TestTrue(TEXT("The real game powers on with both discs"), static_cast<bool>(Powered))) return true;
			(*Powered)->SetSaves(Store.get());
			// Match RenderScreen/normal play's menu, without writing player preferences or quitting the test.
			(*Powered)->SetSettingsKept([Shown = &(*Powered).Get()](const Weltall::FPlayerSettings& Kept) { Shown->ApplySettings(Kept); });
			(*Powered)->SetQuit([]() { UE_LOG(LogGameplayReference, Display, TEXT("The title's Exit was requested during the reference.")); });
			(*Powered)->SetDiscSought([Discs](Weltall::EDiscRelease Release) { return Discs->Find(Release); });
			Screen = MakeShared<FScriptedScreen>(Root, std::move(Store), *Powered);
			if (!Test.TestTrue(TEXT("The shared input, reader and route parse"), Screen->Configure(Params))) return true;
			Screens->Show(Screen.ToSharedRef());
			Started = Now;
			Test.AddInfo(TEXT("Scripted gameplay uses the real renderer/audio and an isolated save store; gameplay acceptance still requires review."));
			return false;
		}
		if (!Screen->Finished())
		{
			if (Now - Started < Screen->Limit() * 3.0 + 120.0) return false;
			Test.AddError(TEXT("Scripted reference exceeded the wall timeout; inspect focus/progress before retrying")); return true;
		}
		if (FinishedAt == 0.0) FinishedAt = Now;
		if (!Screen->ProfileReady())
		{
			if (Now - FinishedAt < 15.0) return false;
			Test.AddError(TEXT("Reference CSV capture did not finish writing")); return true;
		}
		if (Now - FinishedAt < 2.0) return false; // Let the real viewport finish writing its final capture.
		Test.TestFalse(TEXT("No stopped loop or failed trace write"), Screen->Failed());
		TArray<uint8> Source;
		Test.TestTrue(TEXT("The staged input save is unchanged"), FFileHelper::LoadFileToArray(Source, *FPaths::Combine(Root, TEXT("input.sav"))) && Source == Input);
		const auto Loaded = Screen->Saves().ReadBytes(0);
		Test.TestTrue(TEXT("The isolated load slot is unchanged"), Loaded && Loaded->has_value()
			&& **Loaded == std::vector<Weltall::uint8>(Input.GetData(), Input.GetData() + Input.Num()));
		Test.TestTrue(TEXT("The real viewport wrote the final game capture"), IFileManager::Get().FileSize(*FPaths::Combine(Root, TEXT("frame.png"))) > 0);
		Test.AddInfo(FString::Printf(TEXT("Reference finished: simulated=%.3f wall=%.3f"), Screen->Seconds(), Now - Started));
		return true;
	}
private:
	FAutomationTestBase& Test;
	TSharedPtr<FScriptedScreen> Screen;
	FString Root;
	TArray<uint8> Input;
	double Started = FPlatformTime::Seconds(), FinishedAt = 0.0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidGameplayReferenceTest, "Weltall.Android.GameplayReference",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FAndroidGameplayReferenceTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<WeltallAndroidReference::FReferenceCommand>(*this));
	return true;
}

#endif
