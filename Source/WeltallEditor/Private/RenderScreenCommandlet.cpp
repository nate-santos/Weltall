#include "RenderScreenCommandlet.h"

#include "Audio.h"
#include "Battle/BattleRuntime.h"
#include "Game/BattleGameProgram.h"
#include "Game/FieldProgram.h"
#include "Game/FieldMovie.h"
#include "Game/GameLoop.h"
#include "Game/GameSession.h"
#include "Game/PlayerSettings.h"
#include "Game/WorldMapProgram.h"
#include "GlobalShader.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Media/DiscAssets.h"
#include "Media/GameFileIndex.h"
#include "Media/GameLibrary.h"
#include "Shell/GameDiscsSubsystem.h"
#include "Media/MoviePlayer.h"
#include "Media/PsxExecutable.h"
#include "Misc/FileHelper.h"
#include "FrameProfile.h"
#include "Rendering/TextureModLibrary.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Mods/GameMods.h"
#include "OffscreenRender.h"
#include "MenuIconSheet.h"
#include "Reference/ReferenceInput.h"
#include "Reference/RenderRoute.h"
#include "RenderStaging.h"
#include "RenderingThread.h"
#include "Screens/FieldSceneryScreen.h"
#include "Screens/GameLoopScreen.h"
#include "Screens/MovieScreen.h"
#include "Screens/WorldMapScreen.h"
#include "Shell/TextModFiles.h"
#include "ShaderCompiler.h"
#include "Sound/GameSoundSession.h"
#include "Sound/UnheardGameSound.h"
#include "Story/StoryPacks.h"
#include "WorldMap/WorldMapPlayerCamera.h"

#include <vector>

DEFINE_LOG_CATEGORY_STATIC(LogRenderScreen, Log, All);

namespace
{
	constexpr double TickSeconds = 1.0 / 60.0;

	using WeltallReference::FPadStep;
	using WeltallReference::FRenderReader;
	using WeltallReference::ParsePadScript;

	/** Optional reference diagnostics: observe the normal loop without staging or changing its state. */
	void TraceGame(FGameLoopScreen& Game, double Elapsed, Weltall::FPadState Held)
	{
		const Weltall::FGameLoop* Loop = Game.Loop();
		if (Loop == nullptr)
		{
			return;
		}
		UE_LOG(LogRenderScreen, Display, TEXT("GameTrace %.3f stage=%d program=%d pad=%04X"), Elapsed,
			static_cast<int32>(Loop->Stage()), static_cast<int32>(Loop->Program()), Held.Buttons);
		if (const Weltall::FFieldProgram* Field = Loop->Field())
		{
			const Weltall::FEventContext& Context = Field->Runtime().Context();
			if (Context.LeaderActor < Context.States.size())
			{
				const auto& At = Context.States[Context.LeaderActor].Position;
				const Weltall::FFieldMovie* Movie = Field->Movie();
				UE_LOG(LogRenderScreen, Display, TEXT("GameTrace field map=%d leader=%d position=%d,%d,%d lock=%04X yaw=%d disc=%d movie=%d"),
					Context.RunningMap, Context.LeaderActor, At[0] >> 16, At[1] >> 16, At[2] >> 16, Context.PlayerLock, Context.Camera.GoalViewYaw,
					Context.Disc, Movie ? static_cast<int32>(Movie->Request().Movie) : -1);
				if (Movie)
				{
					UE_LOG(LogRenderScreen, Display, TEXT("GameTrace field movie picture=%u view=%d"),
						Movie->Player().PictureNumber(), static_cast<int32>(Movie->View()));
				}
			}
		}
		if (const Weltall::FBattleGameProgram* Battle = Loop->Battle())
		{
			const Weltall::FBattleContext& Context = Battle->Runtime().Context();
			UE_LOG(LogRenderScreen, Display, TEXT("GameTrace battle actor=%d menu=%d end=%d waits=%d,%d,%d"),
				Context.Party.Actor, Context.Party.MenuState, Context.End, Context.Waits[0], Context.Waits[1], Context.Waits[2]);
		}
		if (const Weltall::FWorldMapProgram* World = Loop->WorldMap())
		{
			const Weltall::FWorldMapContext& Context = World->Runtime().Context();
			const Weltall::FWorldMapVector& Leader = Context.Tasks[Weltall::FWorldMapContext::LeaderSlot].Position;
			const Weltall::FWorldMapVector& Vehicle = Context.Tasks[Weltall::FWorldMapContext::VehicleSlot].Position;
			UE_LOG(LogRenderScreen, Display, TEXT("GameTrace world travel=%d leader=%d,%d,%d vehicle=%d,%d,%d yaw=%d roll=%d place=%d notice=%d"),
				Context.TravelMode, Leader.X >> 12, Leader.Y >> 12, Leader.Z >> 12, Vehicle.X >> 12, Vehicle.Y >> 12, Vehicle.Z >> 12,
				Context.CameraYaw, Context.CameraRoll, Context.Place, Context.Notice);
		}
	}

	/** The game's sound beside the screen, as in the game: nothing hears it here, so it is made and dropped as time passes. */
	struct FRenderSound
	{
		const Weltall::FIso9660* Disc = nullptr;
		std::unique_ptr<Weltall::FGameSoundSession> Session;
		std::unique_ptr<Weltall::FUnheardGameSound> Unheard;
		/** With -SoundOut: the samples the render's time made, left and right interleaved, kept for the file; silence before the sound starts. */
		bool bKept = false;
		TArray<int16> Kept;
		double Owed = 0.0;

		/** Starts it afresh, as the console's start-up does. */
		Weltall::FGameSoundSession* Start()
		{
			Unheard.reset();
			Session.reset();
			auto Started = Weltall::FGameSoundSession::Start(*Disc);
			if (!Started)
			{
				UE_LOG(LogRenderScreen, Error, TEXT("The game's sound cannot start: %hs"), Started.Error().Message.c_str());
				return nullptr;
			}
			Session = std::move(*Started);
			Unheard = std::make_unique<Weltall::FUnheardGameSound>(*Session->Sound());
			return Session.get();
		}

		void Advance(double Seconds)
		{
			if (!bKept)
			{
				if (Unheard)
				{
					Unheard->Advance(Seconds);
				}
				return;
			}
			Owed += Seconds * Weltall::IGameSound::SampleRate;
			const int32 Frames = static_cast<int32>(Owed);
			Owed -= Frames;
			const int32 First = Kept.AddZeroed(Frames * 2);
			if (Session)
			{
				Session->Sound()->Render(std::span<int16>(Kept.GetData() + First, static_cast<std::size_t>(Frames) * 2));
			}
		}

		/** The samples kept, as a WAV file (44.1 kHz, 16-bit stereo). */
		bool Save(const FString& Path) const
		{
			TArray<uint8> Wave;
			SerializeWaveFile(Wave, reinterpret_cast<const uint8*>(Kept.GetData()), Kept.Num() * sizeof(int16), 2, Weltall::IGameSound::SampleRate);
			return FFileHelper::SaveArrayToFile(Wave, *Path);
		}
	};


	/** Sets Value to the position of the option's text among Names, leaving it when the option is absent. */
	template <typename TEnum>
	bool ParseChoice(const FString& Params, const TCHAR* Option, std::initializer_list<const TCHAR*> Names, TEnum& Value)
	{
		FString Text;
		if (!FParse::Value(*Params, Option, Text))
		{
			return true;
		}
		int32 Index = 0;
		for (const TCHAR* Name : Names)
		{
			if (Text == Name)
			{
				Value = static_cast<TEnum>(Index);
				return true;
			}
			++Index;
		}
		UE_LOG(LogRenderScreen, Error, TEXT("Unknown %s%s"), Option, *Text);
		return false;
	}

	/** A comma-separated list of numbers given to an option; empty when it is absent. */
	TArray<int32> ParseNumbers(const FString& Params, const TCHAR* Option)
	{
		FString Text;
		TArray<int32> Numbers;
		if (FParse::Value(*Params, Option, Text, false))
		{
			TArray<FString> Parts;
			Text.ParseIntoArray(Parts, TEXT(","));
			for (const FString& Part : Parts)
			{
				Numbers.Add(FCString::Atoi(*Part));
			}
		}
		return Numbers;
	}

	/**
	 * The presentation's options: -Sort=Painter|Depth|Scenery -Filter=Nearest|Crisp|Bilinear|Upscaled -SpriteFilter=Nearest|Crisp|Bilinear|Upscaled -AA=None|MSAA|SSAA
	 * -Cull=Original|InRange -MovieFilter=Smooth|Upscaled.
	 */
	bool ParseFieldOptions(const FString& Params, FFieldRenderOptions& Options)
	{
		return ParseChoice(Params, TEXT("Sort="), {TEXT("Painter"), TEXT("Depth"), TEXT("Scenery")}, Options.Sorting)
			&& ParseChoice(Params, TEXT("MovieFilter="), {TEXT("Smooth"), TEXT("Upscaled")}, Options.MovieFilter)
			&& ParseChoice(Params, TEXT("Filter="), {TEXT("Nearest"), TEXT("Crisp"), TEXT("Bilinear"), TEXT("Upscaled")}, Options.Filter)
			&& ParseChoice(Params, TEXT("SpriteFilter="), {TEXT("Nearest"), TEXT("Crisp"), TEXT("Bilinear"), TEXT("Upscaled")}, Options.SpriteFilter)
			&& ParseChoice(Params, TEXT("AA="), {TEXT("None"), TEXT("MSAA"), TEXT("SSAA")}, Options.AntiAliasing)
			&& ParseChoice(Params, TEXT("Cull="), {TEXT("Original"), TEXT("InRange")}, Options.Culling);
	}

	/**
	 * The settings a played game starts with: the defaults, the presentation's options, the picture's
	 * shape -Aspect=Window|16:9|4:3 and CRT filter -Crt=Off|Monitor|TV|Custom (its parts by -Setting=crt.*),
	 * the player's framing of the field -ViewZoom=factor -ViewTilt=degrees -ViewTurn=degrees, and the
	 * speed battles start at -BattleSpeed=Normal|2x|4x (the pad script's R1 and L1 step it in one), the
	 * text speed -TextSpeed=Normal|2x|4x|Instant (a new player's 2x unless given), and -Setting=key=value;key=value
	 * lines of the settings file read over all of that (-Setting=fix.scene_pictures=false: one fix as the original).
	 */
	bool ParseSettings(const FString& Params, Weltall::FPlayerSettings& Settings)
	{
		Weltall::FFieldCameraOffsets& Camera = Settings.Camera;
		FParse::Value(*Params, TEXT("ViewZoom="), Camera.Zoom);
		FParse::Value(*Params, TEXT("ViewTilt="), Camera.Tilt);
		FParse::Value(*Params, TEXT("ViewTurn="), Camera.Turn);
		Camera.Zoom = FMath::Clamp(Camera.Zoom, Weltall::FFieldCameraOffsets::MinZoom, Weltall::FFieldCameraOffsets::MaxZoom);
		Camera.Tilt = FMath::Clamp(Camera.Tilt, Weltall::FFieldCameraOffsets::MinTilt, Weltall::FFieldCameraOffsets::MaxTilt);
		if (!ParseChoice(Params, TEXT("BattleSpeed="), {TEXT("Normal"), TEXT("2x"), TEXT("4x")}, Settings.BattleSpeed)
			|| !ParseChoice(Params, TEXT("TextSpeed="), {TEXT("Normal"), TEXT("2x"), TEXT("4x"), TEXT("Instant")}, Settings.TextSpeed)
			|| !ParseChoice(Params, TEXT("Aspect="), {TEXT("Window"), TEXT("16:9"), TEXT("4:3")}, Settings.Display.Aspect)
			|| !ParseChoice(Params, TEXT("Crt="), {TEXT("Off"), TEXT("Monitor"), TEXT("TV"), TEXT("Custom")}, Settings.Display.Crt)
			|| !ParseFieldOptions(Params, Settings.Field))
		{
			return false;
		}
		FString Lines;
		if (FParse::Value(*Params, TEXT("Setting="), Lines, false))
		{
			// The settings file's reader takes a key's last line.
			std::string Text = Weltall::WritePlayerSettings(Settings);
			TArray<FString> Each;
			Lines.ParseIntoArray(Each, TEXT(";"));
			for (const FString& Line : Each)
			{
				Text.append(TCHAR_TO_UTF8(*Line)).push_back('\n');
			}
			Settings = Weltall::ReadPlayerSettings(Text);
		}
		return true;
	}

	/**
	 * A field map's scenery alone, standing still: -Map=N; the viewpoint -Entrance=k (entrance 0
	 * unless a target is given), or -Target=x,z [-Layer=L] or -Target=x,y,z, and -Yaw=0..7, or a
	 * script's camera -Eye=x,y,z looking at the target; the camera settings and render state a map's
	 * scripts set: -Pitch=degrees -Projection=H -Zoom=4096ths -Background=r,g,b -CullMargins=x,y.
	 */
	TSharedPtr<IGameScreen> OpenFieldScenery(const FString& Params, const Weltall::FIso9660& Disc, int32 Map, const FFieldRenderOptions& Options)
	{
		FFieldViewpoint Viewpoint;
		const TArray<int32> Target = ParseNumbers(Params, TEXT("Target="));
		if (!Target.IsEmpty())
		{
			if (Target.Num() != 2 && Target.Num() != 3)
			{
				UE_LOG(LogRenderScreen, Error, TEXT("-Target takes x,z or x,y,z"));
				return nullptr;
			}
			Viewpoint.X = Target[0];
			Viewpoint.Z = Target.Last();
			if (Target.Num() == 3)
			{
				Viewpoint.Y = Target[1];
			}
		}
		int32 Entrance = 0;
		if (FParse::Value(*Params, TEXT("Entrance="), Entrance) || Target.IsEmpty())
		{
			Viewpoint.Entrance = Entrance;
		}
		const TArray<int32> Eye = ParseNumbers(Params, TEXT("Eye="));
		if (!Eye.IsEmpty())
		{
			if (Eye.Num() != 3)
			{
				UE_LOG(LogRenderScreen, Error, TEXT("-Eye takes x,y,z"));
				return nullptr;
			}
			Viewpoint.Eye = Weltall::FFieldPoint{Eye[0], Eye[1], Eye[2]};
		}
		FParse::Value(*Params, TEXT("Layer="), Viewpoint.Layer);
		FParse::Value(*Params, TEXT("Yaw="), Viewpoint.YawStep);
		FParse::Value(*Params, TEXT("Pitch="), Viewpoint.Pitch);
		FParse::Value(*Params, TEXT("Projection="), Viewpoint.ProjectionDistance);
		FParse::Value(*Params, TEXT("Zoom="), Viewpoint.DistanceScale);
		Weltall::FFieldRenderState State;
		const TArray<int32> Background = ParseNumbers(Params, TEXT("Background="));
		const TArray<int32> Margins = ParseNumbers(Params, TEXT("CullMargins="));
		if ((!Background.IsEmpty() && Background.Num() != 3) || (!Margins.IsEmpty() && Margins.Num() != 2))
		{
			UE_LOG(LogRenderScreen, Error, TEXT("-Background takes r,g,b and -CullMargins x,y"));
			return nullptr;
		}
		if (!Background.IsEmpty())
		{
			State.BackgroundColor = {static_cast<uint8>(Background[0]), static_cast<uint8>(Background[1]), static_cast<uint8>(Background[2])};
		}
		if (!Margins.IsEmpty())
		{
			State.CullMarginX = Margins[0];
			State.CullMarginY = Margins[1];
		}
		return FFieldSceneryScreen::Open(Disc, Map, Viewpoint, Options, State);
	}

	/** The game started, kept in Game; null, its reason logged, when it cannot start. */
	TSharedPtr<IGameScreen> StartedGame(Weltall::TResult<TSharedRef<FGameLoopScreen>> Started, TSharedPtr<FGameLoopScreen>& Game)
	{
		if (!Started)
		{
			UE_LOG(LogRenderScreen, Error, TEXT("The game cannot start: %hs"), Started.Error().Message.c_str());
			return nullptr;
		}
		Game = *Started;
		return Game;
	}

	/**
	 * -Map=N played as a new game enters it (the game in Game; -FieldStory=N the story's progress it is entered with, its
	 * chapter's card taken as seen, and
	 * -FieldEntrance=K the entrance, where a scene needs them; -FieldParty=a,b,c the party it is entered with (255 an
	 * empty slot) and -FieldGears=g,h,... the gear of each character from Fei on (255: its own stays), for a map in
	 * gears; -FieldCards=Folder a folder of memory card images or the port's own save files, the
	 * last of whose saves from before that point of the story the game is loaded from first; -StoryFolder=Folder the
	 * port's story read from that folder of packs in place of the project's, for a draft; -StoryMode=Original|Expanded
	 * how the game tells the second disc), or with a viewpoint option its scenery alone; and the presentation's options.
	 */
	TSharedPtr<IGameScreen> OpenField(const FString& Params, const Weltall::FIso9660& Disc, FRenderSound& Sound, TSharedPtr<FGameLoopScreen>& Game)
	{
		int32 Map = 0;
		FParse::Value(*Params, TEXT("Map="), Map);
		Weltall::FPlayerSettings Settings;
		if (!ParseSettings(Params, Settings))
		{
			return nullptr;
		}
		FString Unused;
		for (const TCHAR* Viewpoint : {TEXT("Entrance="), TEXT("Target="), TEXT("Eye=")})
		{
			if (FParse::Value(*Params, Viewpoint, Unused))
			{
				return OpenFieldScenery(Params, Disc, Map, Settings.Field);
			}
		}
		int32 Story = -1;
		int32 Entrance = -1;
		FParse::Value(*Params, TEXT("FieldStory="), Story);
		FParse::Value(*Params, TEXT("FieldEntrance="), Entrance);
		const TArray<int32> Party = ParseNumbers(Params, TEXT("FieldParty="));
		const TArray<int32> Gears = ParseNumbers(Params, TEXT("FieldGears="));
		if (Party.Num() != 0 && Party.Num() != 3)
		{
			UE_LOG(LogRenderScreen, Error, TEXT("-FieldParty takes a,b,c"));
			return nullptr;
		}
		FString Cards;
		FParse::Value(*Params, TEXT("FieldCards="), Cards, false);
		Weltall::EStoryMode StoryMode = Weltall::EStoryMode::Unchosen;
		if (!ParseChoice(Params, TEXT("StoryMode="), {TEXT("Unchosen"), TEXT("Original"), TEXT("Expanded")}, StoryMode))
		{
			return nullptr;
		}
		FString StoryFolder;
		TSharedPtr<Weltall::FStoryPacks> Draft;
		if (FParse::Value(*Params, TEXT("StoryFolder="), StoryFolder, false))
		{
			auto Read = Weltall::FStoryPacks::Read(std::filesystem::path(*FPaths::ConvertRelativePathToFull(StoryFolder)));
			if (!Read)
			{
				UE_LOG(LogRenderScreen, Error, TEXT("-StoryFolder cannot be read: %hs"), Read.Error().Message.c_str());
				return nullptr;
			}
			UE_LOG(LogRenderScreen, Display, TEXT("Story documents read from -StoryFolder: %d"), Read->DocumentCount());
			Draft = MakeShared<Weltall::FStoryPacks>(MoveTemp(*Read));
		}
		auto Started = FGameLoopScreen::EnterField(Disc, [&Sound]() { return Sound.Start(); }, static_cast<uint16>(Map), Settings, [Story, Entrance, Cards, Draft, StoryMode, Party, Gears](Weltall::FGameSession& Session)
		{
			if (Draft.IsValid())
			{
				Session.SetStory(Draft.Get());
			}
			if (!Cards.IsEmpty())
			{
				LoadSaveOfStory(Session, Cards, Story >= 0 ? Story : MAX_int32);
			}
			Weltall::FGameState& State = Session.Game();
			if (StoryMode != Weltall::EStoryMode::Unchosen)
			{
				State.SetStoryMode(StoryMode);
			}
			if (Story >= 0)
			{
				State.SetSavedEventVariable(0, static_cast<uint16>(Story));
				// As a game loaded there goes on: without the card of the chapter it is already in.
				Session.ChapterCard().Know(Session.Chapter(), Session.Subtitle());
			}
			if (Entrance >= 0)
			{
				State.SetSavedEventVariable(1, static_cast<uint16>(Entrance));
				State.SetFieldEntrance(static_cast<uint16>(Entrance));
			}
			if (Party.Num() == 3)
			{
				State.SetParty({static_cast<uint8>(Party[0]), static_cast<uint8>(Party[1]), static_cast<uint8>(Party[2])});
			}
			constexpr int32 OwnGear = 255;
			for (int32 Character = 0; Character < Gears.Num(); ++Character)
			{
				if (Gears[Character] != OwnGear)
				{
					State.SetCharacterGear(Character, static_cast<uint8>(Gears[Character]));
				}
			}
		});
		return StartedGame(MoveTemp(Started), Game);
	}

	/** The world map's presentation options: -Sort=Painter|Depth|Scenery -Filter=Nearest|Crisp|Bilinear|Upscaled -SpriteFilter=Nearest|Crisp|Bilinear|Upscaled -AA=None|MSAA|SSAA -Cull=Original|InRange|Extended. */
	bool ParseWorldMapOptions(const FString& Params, FWorldMapRenderOptions& Options)
	{
		return ParseChoice(Params, TEXT("Sort="), {TEXT("Painter"), TEXT("Depth"), TEXT("Scenery")}, Options.Sorting)
			&& ParseChoice(Params, TEXT("Filter="), {TEXT("Nearest"), TEXT("Crisp"), TEXT("Bilinear"), TEXT("Upscaled")}, Options.Filter)
			&& ParseChoice(Params, TEXT("SpriteFilter="), {TEXT("Nearest"), TEXT("Crisp"), TEXT("Bilinear"), TEXT("Upscaled")}, Options.SpriteFilter)
			&& ParseChoice(Params, TEXT("AA="), {TEXT("None"), TEXT("MSAA"), TEXT("SSAA")}, Options.AntiAliasing)
			&& ParseChoice(Params, TEXT("Cull="), {TEXT("Original"), TEXT("InRange"), TEXT("Extended")}, Options.Culling);
	}

	/**
	 * The world map entered as the field leaves for it: -Story=N (event variable 0, which picks the era)
	 * -Location=N -Entrance=N -Yaw=N -Scene=N -Party=a,b,c -Order=a,b,... (the order the player keeps it in) -Gears (the party in its gears) and the
	 * Yggdrasil's -Vehicle=x,y,z,heading -VehicleFlags=N; -Fixes=Original for the world map without its
	 * fixes; -ViewZoom=Z -ViewTilt=D -ViewTurn=D the player's framing it starts under; and the presentation's
	 * options.
	 */
	TSharedPtr<IGameScreen> OpenWorldMap(const FString& Params, const Weltall::FIso9660& Disc, FRenderSound& Sound)
	{
		FWorldMapRenderOptions Options;
		if (!ParseWorldMapOptions(Params, Options))
		{
			return nullptr;
		}
		FWorldMapStart Start;
		int32 Value = 0;
		if (FParse::Value(*Params, TEXT("Story="), Value))
		{
			Start.Story = static_cast<uint16>(Value);
		}
		if (FParse::Value(*Params, TEXT("Location="), Value))
		{
			Start.Location = static_cast<uint16>(Value);
		}
		if (FParse::Value(*Params, TEXT("Entrance="), Value))
		{
			Start.Entrance = static_cast<uint16>(Value);
		}
		if (FParse::Value(*Params, TEXT("Yaw="), Value))
		{
			Start.Yaw = static_cast<uint16>(Value);
		}
		if (FParse::Value(*Params, TEXT("Scene="), Value))
		{
			Start.Scene = static_cast<uint16>(Value);
		}
		if (FParse::Value(*Params, TEXT("VehicleFlags="), Value))
		{
			Start.VehicleFlags = static_cast<uint16>(Value);
		}
		Start.bInGears = FParse::Param(*Params, TEXT("Gears"));
		// The gears' models as the player's settings have them (-Setting=field.gear_models=...).
		Weltall::FPlayerSettings Settings;
		if (!ParseSettings(Params, Settings))
		{
			return nullptr;
		}
		Start.GearModels = Settings.Field.GearModels;
		bool bOriginal = false;
		if (!ParseChoice(Params, TEXT("Fixes="), {TEXT("On"), TEXT("Original")}, bOriginal))
		{
			return nullptr;
		}
		Start.Fixes = bOriginal ? Weltall::FWorldMapOptions::Original() : Weltall::FWorldMapOptions();
		FParse::Value(*Params, TEXT("ViewZoom="), Start.View.Zoom);
		FParse::Value(*Params, TEXT("ViewTilt="), Start.View.Tilt);
		FParse::Value(*Params, TEXT("ViewTurn="), Start.View.Turn);
		Start.View = Weltall::MoveWorldMapCameraOffsets(Start.View, Weltall::FFieldViewInput{});
		const TArray<int32> Party = ParseNumbers(Params, TEXT("-Party="));
		const TArray<int32> Vehicle = ParseNumbers(Params, TEXT("Vehicle="));
		if ((!Party.IsEmpty() && Party.Num() != 3) || (!Vehicle.IsEmpty() && Vehicle.Num() != 4))
		{
			UE_LOG(LogRenderScreen, Error, TEXT("-Party takes a,b,c and -Vehicle x,y,z,heading"));
			return nullptr;
		}
		for (int32 Slot = 0; Slot < Party.Num(); ++Slot)
		{
			Start.Party[static_cast<std::size_t>(Slot)] = static_cast<uint8>(Party[Slot]);
		}
		std::vector<uint8> Shown;
		for (const int32 Character : ParseNumbers(Params, TEXT("Order=")))
		{
			Shown.push_back(static_cast<uint8>(Character));
		}
		Start.Order.Arrange(Start.Party, Shown);
		if (!Vehicle.IsEmpty())
		{
			Start.Vehicle = std::array<int16, 4>{static_cast<int16>(Vehicle[0]), static_cast<int16>(Vehicle[1]), static_cast<int16>(Vehicle[2]),
				static_cast<int16>(Vehicle[3])};
		}
		return FWorldMapScreen::Open(Disc, [&Sound]() { return Sound.Start(); }, Start, Options);
	}


	/** The screen -Screen names; a game played, also in Game. */
	TSharedPtr<IGameScreen> OpenScreen(const FString& Params, const Weltall::FIso9660& Disc, FRenderSound& Sound, TSharedPtr<FGameLoopScreen>& Game)
	{
		FString Name;
		FParse::Value(*Params, TEXT("Screen="), Name);
		if (Name == TEXT("Game"))
		{
			Weltall::FPlayerSettings Settings;
			if (!ParseSettings(Params, Settings))
			{
				return nullptr;
			}
			return StartedGame(FGameLoopScreen::PowerOn(Disc, [&Sound]() { return Sound.Start(); }, Settings), Game);
		}
		if (Name == TEXT("Movie"))
		{
			int32 Movie = 0;
			int32 EndFrame = Weltall::FMoviePlayback::DefaultEndFrame;
			FParse::Value(*Params, TEXT("Movie="), Movie);
			FParse::Value(*Params, TEXT("EndFrame="), EndFrame);
			FFieldRenderOptions Options;
			if (!ParseFieldOptions(Params, Options))
			{
				return nullptr;
			}
			Weltall::FGameSoundSession* Session = Sound.Start();
			return Session != nullptr ? FMovieScreen::Open(Disc, Movie, static_cast<uint32>(EndFrame), false, *Session->Sound(), Options.MovieFilter) : nullptr;
		}
		if (Name == TEXT("Field"))
		{
			return OpenField(Params, Disc, Sound, Game);
		}
		if (Name == TEXT("WorldMap"))
		{
			return OpenWorldMap(Params, Disc, Sound);
		}
		if (Name == TEXT("MenuIcons"))
		{
			return MakeShared<FMenuIconSheetScreen>();
		}
		UE_LOG(LogRenderScreen, Error, TEXT("Unknown screen '%s' (Game, Movie, Field, WorldMap, MenuIcons)"), *Name);
		return nullptr;
	}

	/** Renders the screen's frame as it stands and writes it as a PNG. */
	bool SaveFrame(IGameScreen& Screen, FIntPoint Size, const Weltall::FDisplayOptions& Display, const FString& Out)
	{
		TArray<FColor> Pixels = RenderScreenFrameOffscreen(*Screen.CaptureFrame(), Size, Display);
		FlushRenderingCommands();
		if (Pixels.Num() != Size.X * Size.Y)
		{
			UE_LOG(LogRenderScreen, Error, TEXT("The frame could not be read back"));
			return false;
		}
		for (FColor& Pixel : Pixels)
		{
			Pixel.A = 255;
		}
		IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Png = Wrappers.CreateImageWrapper(EImageFormat::PNG);
		Png->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Size.X, Size.Y, ERGBFormat::BGRA, 8);
		if (!FFileHelper::SaveArrayToFile(Png->GetCompressed(), *Out))
		{
			UE_LOG(LogRenderScreen, Error, TEXT("Cannot write %s"), *Out);
			return false;
		}
		return true;
	}
}

URenderScreenCommandlet::URenderScreenCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URenderScreenCommandlet::Main(const FString& Params)
{
	FString Out;
	FString SizeText = TEXT("1920x1080");
	double Seconds = 0.0;
	FParse::Value(*Params, TEXT("Out="), Out);
	FParse::Value(*Params, TEXT("Size="), SizeText);
	FParse::Value(*Params, TEXT("Seconds="), Seconds);
	FString WidthText;
	FString HeightText;
	if (Out.IsEmpty() || !SizeText.Split(TEXT("x"), &WidthText, &HeightText))
	{
		UE_LOG(LogRenderScreen, Error, TEXT("Usage: -run=RenderScreen -Screen=Game|Movie|Field|WorldMap|MenuIcons [-Movie=N] [-EndFrame=N] [-Map=N [-Entrance=k|-Target=x,z|x,y,z -Layer=L -Yaw=0..7 ")
			TEXT("-Eye=x,y,z -Pitch=D -Projection=H -Zoom=Z -Background=r,g,b -CullMargins=x,y] -Sort=Painter|Depth|Scenery -Filter=Nearest|Crisp|Bilinear|Upscaled -SpriteFilter=Nearest|Crisp|Bilinear|Upscaled -MovieFilter=Smooth|Upscaled ")
			TEXT("-AA=None|MSAA|SSAA -Cull=Original|InRange -ViewZoom=Z -ViewTilt=D -ViewTurn=D] [-Story=N -Location=N -Entrance=N -Yaw=N -Scene=N -Party=a,b,c -Gears ")
			TEXT("-Vehicle=x,y,z,h -VehicleFlags=N -Cull=Original|InRange|Extended] [-State=Full|Weak|Strong|Rising] [-Menu=Mode,Parameter -MenuAt=S] [-Battle=Formation,Kind -BattleAt=S -BattleEvery=S -Party=a,b,c -Ride=a,b,c -GearOf=c:g] [-Arena=From,Kind,Gear1,Gear2,Wins,Level -ArenaAt=S] ")
			TEXT("[-FieldStory=N -FieldEntrance=K -FieldParty=a,b,c -FieldGears=g,h,... -FieldCards=Folder -StoryFolder=Folder -StoryMode=Original|Expanded] [-Disc=1|2 [-Undub]] [-Card=Chapter[,Subtitle] -CardAt=S] [-MenuArea=Screen|Wide] [-Icons=Set] [-Saves=Folder] [-Keys=a,b] [-MenuAssets] [-Setting=key=value;...] ")
			TEXT("[-Pad=a,b+c,...|-PadFile=Path -PadStep=S] [-Route=x,z;!x,z;... [-RouteAt=S] [-RouteBattles]] [-Reader=Base,PerGlyph] [-GameTrace=S] [-SoundOut=path.wav] [-BattleSpeed=Normal|2x|4x] [-TextSpeed=Normal|2x|4x|Instant] [-Textures=Folder] [-Mods=Folder] [-Assets=Folder] [-Games=Folder] -Seconds=S [-Every=S] [-Profile=Hz [-ProfileGpuAt=S]] [-From=S] -Size=WxH [-Aspect=Window|16:9|4:3] [-Crt=Off|Monitor|TV|Custom] -Out=path.png"));
		return 1;
	}
	const FIntPoint Size(FCString::Atoi(*WidthText), FCString::Atoi(*HeightText));
	// The picture is drawn as the settings show it, and as the options screen changes them in a game played.
	Weltall::FPlayerSettings Presentation;
	if (!ParseSettings(Params, Presentation))
	{
		return 1;
	}
	Weltall::FDisplayOptions Display = Presentation.Display;
	// A commandlet starts without the global shaders the screens draw with.
	CompileGlobalShaderMap(false);
	if (GShaderCompilingManager != nullptr)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	// Replacement textures from a folder laid out as the game's Assets/textures.
	FString TexturesFolder;
	if (FParse::Value(*Params, TEXT("Textures="), TexturesFolder))
	{
		FTextureModLibrary::Configure(FPaths::ConvertRelativePathToFull(TexturesFolder), true, false);
	}

	// Use the runtime's platform default, with the commandlet's explicit -Games= override when supplied.
	FString GamesFolder = UGameDiscsSubsystem::GamesFolder();
	FParse::Value(*Params, TEXT("Games="), GamesFolder);
	const Weltall::FGameLibrary Library = Weltall::FGameLibrary::Scan(std::filesystem::path(*FPaths::ConvertRelativePathToFull(GamesFolder)));
	int32 DiscNumber = 1;
	FParse::Value(*Params, TEXT("Disc="), DiscNumber);
	// -Undub plays the supported undub's disc in place of the USA's: its movies with its sound and subtitles.
	const bool bUndub = FParse::Param(*Params, TEXT("Undub"));
	const Weltall::FIso9660* Disc = Library.Find(DiscNumber == 2 ? (bUndub ? Weltall::EDiscRelease::UndubDisc2 : Weltall::EDiscRelease::UsaDisc2)
																   : (bUndub ? Weltall::EDiscRelease::UndubDisc1 : Weltall::EDiscRelease::UsaDisc1));
	if (Disc == nullptr)
	{
		UE_LOG(LogRenderScreen, Error, TEXT("Disc %d%s is not in the Games folder"), DiscNumber == 2 ? 2 : 1, bUndub ? TEXT(" (undub)") : TEXT(""));
		return 1;
	}
	// -Mods=Folder: the mods in a folder laid out as the game's Mods, their files given to the discs as the settings choose.
	Weltall::FMenuMods Mods;
	Weltall::FFoundMods Found;
	FString ModsFolder;
	if (FParse::Value(*Params, TEXT("Mods="), ModsFolder))
	{
		Found = Weltall::FindMods(std::filesystem::path(*FPaths::ConvertRelativePathToFull(ModsFolder)));
		if (!Found.PerfectWorks)
		{
			UE_LOG(LogRenderScreen, Warning, TEXT("No mod is in %s"), *ModsFolder);
		}
		if (const std::optional<Weltall::FError> Error = Weltall::ApplyMods(Library, Found, Presentation.PerfectWorks))
		{
			UE_LOG(LogRenderScreen, Error, TEXT("The mods' files cannot be used: %hs"), Error->Message.c_str());
			return 1;
		}
		Mods = {Found.PerfectWorks.has_value(), Found.PerfectWorks ? Found.PerfectWorks->Version() : std::string(), Presentation.PerfectWorks};
	}
	// -Assets=Folder: the player's messages and font sheet in a folder laid out as the game's Assets (its text and font),
	// given to every disc as External assets gives them.
	FString AssetsFolder;
	if (FParse::Value(*Params, TEXT("Assets="), AssetsFolder))
	{
		AssetsFolder = FPaths::ConvertRelativePathToFull(AssetsFolder);
		const std::shared_ptr<const Weltall::FMesFontSheet> Sheet = FindFontSheet(FPaths::Combine(AssetsFolder, TEXT("font")));
		const std::shared_ptr<const Weltall::FMessageOverrides> Messages = FindMessageOverrides(std::filesystem::path(*FPaths::Combine(AssetsFolder, TEXT("text"))),
			Sheet.get());
		for (const Weltall::EDiscRelease Release : Library.Releases())
		{
			const Weltall::FIso9660& Each = *Library.Find(Release);
			const auto Executable = Weltall::ReadBootExecutable(Each);
			const auto Index = Executable ? Weltall::FGameFileIndex::Read(*Executable) : Weltall::TResult<Weltall::FGameFileIndex>(Executable.Error());
			auto Store = Index ? Weltall::FDiscAssetStore::Open(std::filesystem::path(*AssetsFolder), std::string(Weltall::DiscReleaseId(Release)), Each.Image(), *Index)
							   : Weltall::TResult<std::shared_ptr<Weltall::FDiscAssetStore>>(Index.Error());
			if (!Store)
			{
				UE_LOG(LogRenderScreen, Error, TEXT("The assets of %hs cannot be used: %hs"), std::string(Weltall::DiscReleaseId(Release)).c_str(), Store.Error().Message.c_str());
				return 1;
			}
			(*Store)->SetMessages(Messages);
			(*Store)->SetFontSheet(Sheet);
			(*Store)->SetEnabled(true);
			Each.Image().SetAssetStore(*Store);
		}
	}
	FRenderSound Sound;
	Sound.Disc = Disc;
	FString SoundOut;
	Sound.bKept = FParse::Value(*Params, TEXT("SoundOut="), SoundOut, false);
	TArray<FPadStep> PadScript;
	double PadStep = 1.0;
	FRenderStaging Staging;
	FRenderReader Reader;
	FRenderRoute Route;
	if (!ParsePadScript(Params, PadScript, PadStep) || !FRenderStaging::Parse(Params, Staging) || !Reader.Parse(Params) || !FRenderRoute::Parse(Params, Route))
	{
		return 1;
	}
	double Every = 0.0;
	FParse::Value(*Params, TEXT("Every="), Every);
	double GameTrace = 0.0;
	if (FParse::Value(*Params, TEXT("GameTrace="), GameTrace) && (!FMath::IsFinite(GameTrace) || GameTrace < TickSeconds))
	{
		UE_LOG(LogRenderScreen, Error, TEXT("-GameTrace takes an interval of at least 1/60 second"));
		return 1;
	}
	double NextGameTrace = 0.0;
	double NextShot = Every;
	FParse::Value(*Params, TEXT("From="), NextShot);
	TSharedPtr<FGameLoopScreen> Game;
	const TSharedPtr<IGameScreen> Screen = OpenScreen(Params, *Disc, Sound, Game);
	if (!Screen.IsValid())
	{
		return 1;
	}
	if (Game.IsValid())
	{
		// The options screen's settings last the render: the player's own are not written.
		Game->SetSettingsKept([Shown = Game.Get()](const Weltall::FPlayerSettings& Kept) { Shown->ApplySettings(Kept); });
		// The title's Exit is shown as the player has it, and logged rather than closing the render.
		Game->SetQuit([]() { UE_LOG(LogRenderScreen, Display, TEXT("The title's Exit closes the game.")); });
		// The disc the game asks for is found where the player keeps the discs, as in play.
		Game->SetDiscSought([&Library](Weltall::EDiscRelease Wanted) { return Library.Find(Wanted); });
		Game->SetMenuMods(Mods);
		if (Found.PerfectWorks)
		{
			Game->SetPerfectWorksSubtitles({Found.PerfectWorks->MovieSubtitles(1), Found.PerfectWorks->MovieSubtitles(2)}, Presentation.PerfectWorks.bEnabled);
		}
	}
	if (Game.IsValid())
	{
		// The render's glyphs follow the bindings the Controls page changes.
		Game->SetSettingsFollowed([&Staging, &Display](const Weltall::FPlayerSettings& Changed)
		{
			Staging.Glyphs.Follow(Changed.Controls);
			Display = Changed.Display;
		});
	}
	if (!Staging.IsEmpty() && !Game.IsValid())
	{
		UE_LOG(LogRenderScreen, Error, TEXT("-State, -Menu, -Battle, -Arena, -Card, -MenuArea, -Icons, -Saves and -Keys stage a game played: -Screen=Game, or -Screen=Field without a viewpoint"));
		return 1;
	}
	if (Game.IsValid() && (!Staging.IsEmpty() || !Staging.Order.IsEmpty()))
	{
		Staging.Begin(*Game);
	}
	// Profiled, the display takes frames at the rate given, each drawn, and those from -From on timed.
	double ProfileRate = 0.0;
	FParse::Value(*Params, TEXT("Profile="), ProfileRate);
	double ProfileFrom = 0.0;
	FParse::Value(*Params, TEXT("From="), ProfileFrom);
	// Every simulated display frame must draw and retire its uploads, even between PNG captures.
	// The profiling path already supplies those frame boundaries and waits for the GPU. Skipping
	// them queues field/VRAM work indefinitely and can exhaust memory during a long reference.
	double ProfileGpuAt = -1.0;
	FParse::Value(*Params, TEXT("ProfileGpuAt="), ProfileGpuAt);
	FFrameProfile Frames(Size, Display, ProfileFrom, ProfileGpuAt);
	const double DisplaySeconds = ProfileRate > 0.0 ? 1.0 / ProfileRate : TickSeconds;
	for (double Elapsed = 0.0; Elapsed + DisplaySeconds / 2 < Seconds; Elapsed += DisplaySeconds)
	{
		const int32 PadIndex = static_cast<int32>(Elapsed / PadStep);
		FPadStep Step = PadScript.IsValidIndex(PadIndex) ? PadScript[PadIndex] : FPadStep{};
		if (Game.IsValid())
		{
			Staging.Advance(*Game, Elapsed);
			Step.Held.Buttons |= Route.Step(*Game, Elapsed).Buttons;
			if (Reader.bReads && Reader.Holds(*Game, DisplaySeconds))
			{
				Step.Held.Buttons |= Weltall::FPadState::Bit(Weltall::EPadButton::Cross);
				if (Reader.Held == 0.0)
				{
					// Where each page stood whole: the moment before is the shot of it.
					UE_LOG(LogRenderScreen, Display, TEXT("Reader: confirms at %.3f s"), Elapsed);
				}
			}
		}
		Screen->AddViewInput(Step.ViewInput(DisplaySeconds));
		const double UpdateStart = FPlatformTime::Seconds();
		Screen->Update(DisplaySeconds, Step.Held);
		const double Updated = FPlatformTime::Seconds();
		if (GameTrace > 0.0 && Game.IsValid() && Elapsed + DisplaySeconds >= NextGameTrace)
		{
			TraceGame(*Game, Elapsed + DisplaySeconds, Step.Held);
			NextGameTrace += GameTrace;
		}
		// As the display takes a frame every tick: a screen may show its last one again (a battle's transition does).
		const TSharedRef<const IScreenFrame> Frame = Screen->CaptureFrame();
		const double Captured = FPlatformTime::Seconds();
		Sound.Advance(DisplaySeconds);
		Frames.Draw(*Frame, Updated - UpdateStart, Captured - Updated, Elapsed);
		if (Every > 0.0 && Elapsed + DisplaySeconds >= NextShot)
		{
			const FString Shot = FString::Printf(TEXT("%s-%06.2f.png"), *FPaths::GetBaseFilename(Out, false), NextShot);
			if (!SaveFrame(*Screen, Size, Display, Shot))
			{
				return 1;
			}
			NextShot += Every;
		}
	}
	if (ProfileRate > 0.0)
	{
		Frames.Log(ProfileRate);
	}
	if (!SaveFrame(*Screen, Size, Display, Out))
	{
		return 1;
	}
	if (Sound.bKept && !Sound.Save(SoundOut))
	{
		UE_LOG(LogRenderScreen, Error, TEXT("-SoundOut cannot be written: %s"), *SoundOut);
		return 1;
	}
	UE_LOG(LogRenderScreen, Display, TEXT("Rendered %s"), *Out);
	return 0;
}
