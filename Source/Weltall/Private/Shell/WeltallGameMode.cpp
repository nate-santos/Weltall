#include "Shell/WeltallGameMode.h"

#include "Audio/GameSoundSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Foundation/Result.h"
#include "Input/ButtonGlyphSubsystem.h"
#include "Input/KeyListenerSubsystem.h"
#include "Input/WeltallPlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Menu/OptionsText.h"
#include "Screens/GameLoopScreen.h"
#include "Screens/GameScreenSubsystem.h"
#include "Shell/GameDiscsSubsystem.h"
#include "Shell/GameModsSubsystem.h"
#include "Shell/GameAssetsSubsystem.h"
#include "Shell/GameNotice.h"
#include "Shell/GameWindowSubsystem.h"
#include "Shell/PlayerSettingsSubsystem.h"
#include "Shell/SaveStoreSubsystem.h"
#include "TimerManager.h"
#include "WeltallLog.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#include <filesystem>
#include <string>

#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#endif

AWeltallGameMode::AWeltallGameMode()
{
	PlayerControllerClass = AWeltallPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
}

bool AWeltallGameMode::PlayerCanRestart_Implementation(APlayerController*)
{
	return false;
}

void AWeltallGameMode::BeginPlay()
{
	Super::BeginPlay();
	// Nothing in the world moves by physics: its scene is never stepped.
	GetWorld()->bShouldSimulatePhysics = false;
	if (StartGame())
	{
		return;
	}
	if (GetGameInstance()->GetSubsystem<UGameDiscsSubsystem>()->PreparationNotice().IsEmpty())
	{
		UE_LOG(LogWeltall, Error, TEXT("No disc of the game is in %s"), *UGameDiscsSubsystem::GamesFolder());
	}
	ShowNotice(DiscsMissing(), false);
	GetWorldTimerManager().SetTimer(LookAgain, this, &AWeltallGameMode::LookForDiscs, 2.0f, true);
}

void AWeltallGameMode::LookForDiscs()
{
	UGameDiscsSubsystem* Discs = GetGameInstance()->GetSubsystem<UGameDiscsSubsystem>();
	Discs->Rescan();
	if (Discs->Library().PlayableDisc(1) == nullptr && Discs->Library().PlayableDisc(2) == nullptr)
	{
		NoticeText->SetText(DiscsMissing());
		return;
	}
	GetWorldTimerManager().ClearTimer(LookAgain);
	HideNotice();
	StartGame();
}

void AWeltallGameMode::ShowNotice(const FText& Text, bool bOverPicture)
{
	HideNotice();
	const FGameNotice Shown = MakeGameNotice(Text, bOverPicture);
	Notice = Shown.Widget;
	NoticeText = Shown.Text;
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->AddViewportWidgetContent(Shown.Widget);
	}
}

void AWeltallGameMode::HideNotice()
{
	if (!Notice)
	{
		return;
	}
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->RemoveViewportWidgetContent(Notice.ToSharedRef());
	}
	Notice.Reset();
	NoticeText.Reset();
}

FText AWeltallGameMode::DiscsMissing() const
{
	namespace Text = Weltall::OptionsText::Discs;
	const FText Preparing = GetGameInstance()->GetSubsystem<UGameDiscsSubsystem>()->PreparationNotice();
	if (!Preparing.IsEmpty()) return Preparing;
	const Weltall::FGameLibrary& Library = GetGameInstance()->GetSubsystem<UGameDiscsSubsystem>()->Library();
	const std::filesystem::path Folder = std::filesystem::path(*UGameDiscsSubsystem::GamesFolder()).make_preferred();
	// Files by their place in the Games folder.
	const auto Shown = [&Folder](const std::filesystem::path& File)
	{
		const std::filesystem::path Relative = File.lexically_relative(Folder);
		const std::u8string Name = (Relative.empty() ? File : Relative).u8string();
		return std::string(Name.begin(), Name.end());
	};
	const std::u8string FolderName = Folder.u8string();
	std::string Message = std::string(Text::Missing) + "\n\n" + Weltall::OptionsText::Fill(Text::WhereTheyGo, {std::string(FolderName.begin(), FolderName.end())});
	for (const Weltall::EDiscRelease Release : Library.Releases())
	{
		const std::string_view Number = Weltall::DiscNumberOfRelease(Release) == 1 ? "1" : "2";
		Message += "\n\n" + Weltall::OptionsText::Fill(Text::Found, {Number, Shown(Library.FileOf(Release))});
	}
	constexpr std::size_t MostListed = 12;
	if (!Library.Notes().empty())
	{
		Message += "\n\n" + std::string(Text::NotUsed);
		for (std::size_t Index = 0; Index < Library.Notes().size() && Index < MostListed; ++Index)
		{
			const Weltall::FLibraryNote& Note = Library.Notes()[Index];
			Message += "\n" + Weltall::OptionsText::Fill(Text::NotUsedFile, {Shown(Note.File), Note.Text});
		}
		if (Library.Notes().size() > MostListed)
		{
			Message += "\n" + Weltall::OptionsText::Fill(Text::More, {std::to_string(Library.Notes().size() - MostListed)});
		}
	}
	return FText::FromString(FString(UTF8_TO_TCHAR(Message.c_str())));
}

bool AWeltallGameMode::StartGame()
{
	UGameInstance* Game = GetGameInstance();
	UGameDiscsSubsystem* Discs = Game->GetSubsystem<UGameDiscsSubsystem>();
	const Weltall::FIso9660* FirstDisc = Discs->Library().PlayableDisc(1);
	const Weltall::FIso9660* SecondDisc = Discs->Library().PlayableDisc(2);
	const Weltall::FIso9660* Disc = FirstDisc != nullptr ? FirstDisc : SecondDisc;
	if (Disc == nullptr)
	{
		return false;
	}
	Game->GetSubsystem<UGameWindowSubsystem>()->ShowGameIcon(*Disc);
	// The discs found since the game started (the first, looked for again) take the mods' files before they are read.
	UGameModsSubsystem* Mods = Game->GetSubsystem<UGameModsSubsystem>();
	Mods->Refresh();
	// The game runs from power-on, its sound started with it and started again when the console restarts.
	UGameSoundSubsystem* Sound = Game->GetSubsystem<UGameSoundSubsystem>();
	FGameLoopScreen::FStartSound StartSound = [Sound, Disc]() { return Sound->Start(*Disc) ? Sound->Session() : nullptr; };
	UPlayerSettingsSubsystem* Settings = Game->GetSubsystem<UPlayerSettingsSubsystem>();
	// Disc 2 is read beside disc 1 when the player has it, so the disc change's "INSERT DISC 2" is only confirmed;
	// without it the Games folder is looked in again as the game asks for it, as it is for disc 1 when the game
	// starts from disc 2 (its New Game and disc 1's saves ask for disc 1).
	bool bRecoveredLaunch = false;
#if PLATFORM_ANDROID
	if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
	{
		if (jobject Activity = FAndroidApplication::GetGameActivityThis())
		{
			jclass Class = Env->GetObjectClass(Activity);
			jmethodID Consume = Env->GetMethodID(Class, "AndroidThunkJava_WeltallConsumeRecoveryLaunch", "()Z");
			if (Consume && !Env->ExceptionCheck()) bRecoveredLaunch = Env->CallBooleanMethod(Activity, Consume) == JNI_TRUE;
			if (Env->ExceptionCheck()) { Env->ExceptionClear(); bRecoveredLaunch = false; }
			Env->DeleteLocalRef(Class);
		}
	}
	if (bRecoveredLaunch) UE_LOG(LogWeltall, Display, TEXT("Activity recovery: starting a fresh session at the title screen"));
#endif
	auto Started = FGameLoopScreen::PowerOn(*Disc, MoveTemp(StartSound), Settings->Settings(), FirstDisc != nullptr ? SecondDisc : nullptr, bRecoveredLaunch);
	if (!Started)
	{
		UE_LOG(LogWeltall, Error, TEXT("The game cannot start: %hs"), Started.Error().Message.c_str());
		ShowNotice(GameStoppedText(Started.Error(), false), false);
		return true;
	}
	const TSharedRef<FGameLoopScreen> Screen = *Started;
	Screen->SetButtonGlyphs(Game->GetSubsystem<UButtonGlyphSubsystem>());
	Screen->SetSaves(&Game->GetSubsystem<USaveStoreSubsystem>()->Store());
	Screen->SetCameraMoved([Settings](const Weltall::FFieldCameraOffsets& Field, const Weltall::FWorldMapCameraOffsets& WorldMap)
	{
		Settings->KeepCameraOffsets(Field, WorldMap);
	});
	Screen->SetBattleSpeedStepped([Settings](Weltall::EBattleSpeed Speed) { Settings->KeepBattleSpeed(Speed); });
	Screen->SetMenuKeys(Game->GetSubsystem<UKeyListenerSubsystem>());
	UGameAssetsSubsystem* Assets = Game->GetSubsystem<UGameAssetsSubsystem>();
	// Bundled Android discs may have become available after subsystem initialization.
	Assets->Refresh();
	Screen->SetMenuAssets(Assets);
	Screen->SetMenuMods(Mods->MenuMods());
	Screen->SetPerfectWorksSubtitles(Mods->PerfectWorksSubtitles(), Mods->MenuMods().PerfectWorksInUse.bEnabled);
	Screen->SetDisplayModes([Window = Game->GetSubsystem<UGameWindowSubsystem>()]() { return Window->Modes(); });
	Screen->SetSettingsFollowed([Settings](const Weltall::FPlayerSettings& Changed) { Settings->Follow(Changed); });
	Screen->SetSettingsKept([Settings](const Weltall::FPlayerSettings& Kept) { Settings->Change(Kept); });
	Screen->SetQuit([World = TWeakObjectPtr<UWorld>(GetWorld())]()
	{
		if (UWorld* Playing = World.Get())
		{
			UKismetSystemLibrary::QuitGame(Playing, nullptr, EQuitPreference::Quit, false);
		}
	});
	Screen->SetDiscSought([Discs, Assets, Mods](Weltall::EDiscRelease Release)
	{
		Discs->Rescan();
		Assets->Refresh();
		Mods->Refresh();
		return Discs->Find(Release);
	});
	Screen->SetStopped([Mode = TWeakObjectPtr<AWeltallGameMode>(this)](const Weltall::FError& Error)
	{
		if (AWeltallGameMode* Playing = Mode.Get())
		{
			Playing->ShowNotice(GameStoppedText(Error, true), true);
		}
	});
	Game->GetSubsystem<UGameScreenSubsystem>()->Show(Screen);
	SettingsChanged = Settings->OnSettingsChanged.AddLambda([Settings, Weak = Screen.ToWeakPtr()]()
	{
		if (const TSharedPtr<FGameLoopScreen> Shown = Weak.Pin())
		{
			Shown->ApplySettings(Settings->Settings());
		}
	});
	return true;
}

void AWeltallGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(LookAgain);
	HideNotice();
	GetGameInstance()->GetSubsystem<UPlayerSettingsSubsystem>()->OnSettingsChanged.Remove(SettingsChanged);
	GetGameInstance()->GetSubsystem<UGameScreenSubsystem>()->Close();
	Super::EndPlay(EndPlayReason);
}
