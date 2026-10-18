#pragma once

#include <array>
#include <initializer_list>
#include <string>
#include <string_view>

/**
 * Every word the options screen shows, in one place: the pages' titles, each row's label, its values and its three
 * lines of help, and what the pages say back after an action; and the port's other words in the game's menus.
 *
 * The rules the text must keep (the menus' tests check them against the game's font):
 * - a help line holds about 30 letters, and a row's label and value share one line;
 * - the font has letters, digits, spaces and + , - . / ! " # % & ' ( ) : ? [ ] = only (no semicolon);
 * - "{}" is filled in as the game runs, in order: a key's name, a count, a card's name...
 * Lists of values are in the order the setting steps through them.
 */
namespace Weltall::OptionsText
{
	/** A row's label and its help. */
	struct FRow
	{
		std::string_view Label;
		std::array<std::string_view, 3> Help;
	};

	inline constexpr std::string_view On = "On";
	inline constexpr std::string_view Off = "Off";

	namespace Titles
	{
		inline constexpr std::string_view Display = "Display";
		inline constexpr std::string_view Sound = "Sound";
		inline constexpr std::string_view Camera = "Camera";
		inline constexpr std::string_view Fixes = "Fixes";
		inline constexpr std::string_view CustomFixes = "Custom Fixes";
		inline constexpr std::string_view Controls = "Controls";
		inline constexpr std::string_view Saves = "Saves";
		inline constexpr std::string_view Import = "Import";
		inline constexpr std::string_view Assets = "Assets";
	}

	namespace Display
	{
		inline constexpr FRow FullScreen{"Fullscreen", {"Borderless fullscreen. When", "off, the game plays in a", "window of the size below."}};
		inline constexpr FRow Resolution{"Resolution", {"The window's size when not", "in fullscreen.", ""}};
		/** The value: width, then height. */
		inline constexpr std::string_view ResolutionValue = "{} x {}";
		inline constexpr FRow Aspect{"Aspect ratio", {"Auto fills the window. 16:9", "and 4:3 add black bars", "where needed."}};
		inline constexpr std::array<std::string_view, 3> AspectValues{"Auto", "16:9", "4:3"};
		inline constexpr FRow Sorting{"Depth sorting", {"Per pixel sorts characters and", "gears by depth. Scenery too", "sorts the scenery as well."}};
		inline constexpr std::array<std::string_view, 3> SortingValues{"Original", "Per pixel", "Scenery too"};
		inline constexpr FRow SceneryFilter{"Scenery filter", {"Sharp: original. Crisp: steady", "in motion. Smooth: softened.", "Upscaled: smooth diagonals."}};
		inline constexpr FRow SpriteFilter{"Sprite filter", {"Sharp: original. Crisp: steady", "in motion. Smooth: softened.", "Upscaled: smooth diagonals."}};
		inline constexpr std::array<std::string_view, 4> FilterValues{"Sharp", "Crisp", "Smooth", "Upscaled"};
		inline constexpr FRow MovieFilter{"Movie filter", {"Smooth keeps the movies' own", "look. Upscaled cleans them", "up and sharpens their lines."}};
		inline constexpr std::array<std::string_view, 2> MovieFilterValues{"Smooth", "Upscaled"};
		inline constexpr FRow AntiAliasing{"Anti-aliasing", {"Smooths jagged edges. MSAA", "is fast. Supersampling is", "sharper but costs more."}};
		inline constexpr std::array<std::string_view, 3> AntiAliasingValues{"Off", "MSAA", "Supersampling"};
		inline constexpr FRow Culling{"Widescreen drawing", {"Full draws everything in a", "widescreen view. Original", "draws only the 4:3 area."}};
		inline constexpr std::array<std::string_view, 2> CullingValues{"Original", "Full"};
		inline constexpr FRow Pilots{"Pilot sprites", {"In areas sized for Gears,", "Detailed draws the party on", "foot with their own sprites."}};
		/** The values with the party's own sprites on, then off. */
		inline constexpr std::array<std::string_view, 2> PilotsValues{"Detailed", "Original"};
		inline constexpr FRow Gears{"Gear sprites", {"3D draws the Gears with their", "battle models. In play keeps", "the sprites in cutscenes."}};
		inline constexpr std::array<std::string_view, 3> GearsValues{"Original", "3D in play", "3D always"};
		inline constexpr FRow ChapterCards{"Chapter titles", {"Shows each chapter's title", "as the chapter begins.", ""}};
		inline constexpr FRow TextSpeed{"Text speed", {"How fast text appears in", "message boxes. Instant shows", "a whole page at once."}};
		inline constexpr std::array<std::string_view, 4> TextSpeedValues{"Normal", "2x", "4x", "Instant"};
		inline constexpr FRow BattleSpeed{"Battle speed", {"How fast battles play. You", "can also change it in battle", "with Camera left and right."}};
		inline constexpr std::array<std::string_view, 3> BattleSpeedValues{"Normal", "2x", "4x"};
		inline constexpr FRow PauseInBackground{"Pause in background", {"Pauses the game while its", "window isn't in focus. Play", "time stops too."}};
	}

	namespace Sound
	{
		inline constexpr FRow Output{"Sound mode", {"Wide-Stereo spreads the", "sound wider. Mono plays the", "same sound on both sides."}};
		inline constexpr std::array<std::string_view, 3> OutputValues{"Stereo", "Wide-Stereo", "Mono"};
		inline constexpr FRow InBackground{"Background audio", {"Keeps the sound playing", "while the game's window", "isn't in focus."}};
		inline constexpr FRow Voices{"Voices", {"Auto follows the play disc.", "USA or undub discs supply", "voices for new movies/battles."}};
		inline constexpr std::array<std::string_view, 3> VoiceValues{"Auto", "English", "Japanese"};
	}

	namespace Assets
	{
		inline constexpr FRow External{"External assets", {"Loads files from Assets", "beside Games. Missing files", "come from your discs."}};
		inline constexpr FRow Capture{"Capture textures", {"Also saves textures as drawn,", "for any the export can't", "reach. Turn off when done."}};
		inline constexpr FRow Textures{"Export textures", {"Saves every map's and sprite's", "texture as a PNG to edit.", "Existing files are kept."}};
		inline constexpr FRow Export{"Dump disc assets", {"Copies your discs to Assets.", "Existing files are kept.", "Keep dumps on your computer."}};
		inline constexpr FRow Music{"Export music WAVs", {"Renders songs as editable WAV.", "This can use several GB.", "Existing files are kept."}};
		inline constexpr FRow Cancel{"Cancel export", {"Stops after this step.", "Completed files are kept.", "Start again to resume."}};
		inline constexpr FRow Progress{"Export status", {"Files are in Assets beside", "Games. See Docs/Modding.md", "for formats and editing."}};
		inline constexpr std::array<std::string_view, 6> States{"Ready", "Working", "Complete", "Cancelled", "Failed", "Unavailable"};
	}

	namespace Camera
	{
		inline constexpr FRow Zoom{"Zoom", {"How far back the camera sits", "on the field. 100% is the", "original distance."}};
		inline constexpr std::string_view ZoomValue = "{}%";
		inline constexpr FRow Tilt{"Tilt", {"Raises the camera's angle on", "the field, in degrees. 0 is", "the original angle."}};
		inline constexpr FRow Scenes{"Cutscene camera", {"Original switches to the", "game's camera in cutscenes.", "Custom keeps yours if it can."}};
		/** The values with the switch on, then off. */
		inline constexpr std::array<std::string_view, 2> ScenesValues{"Original", "Custom"};
		inline constexpr FRow Follow{"Camera follow", {"Steady keeps the camera close", "to the party. Original lets", "it trail behind."}};
		/** The values with the switch on, then off. */
		inline constexpr std::array<std::string_view, 2> FollowValues{"Steady", "Original"};
		inline constexpr FRow Reset{"Reset camera", {"Puts the zoom, tilt and", "rotation back to default.", ""}};
	}

	namespace Fixes
	{
		inline constexpr std::string_view Label = "Bug fixes";
		inline constexpr std::array<std::string_view, 3> PresetValues{"Port Enhancements", "Original", "Custom"};
		/** The help of each preset, in the order above. */
		inline constexpr std::array<std::array<std::string_view, 3>, 3> PresetHelp{{
			{"Fixes the original game's", "bugs.", ""},
			{"Plays exactly like the", "original, bugs and all.", ""},
			{"Press Confirm to choose", "each fix yourself.", ""},
		}};
		/** A fix's value, on and off. */
		inline constexpr std::string_view FixOn = "On";
		inline constexpr std::string_view FixOff = "Original";
	}

	/** The Custom Fixes list: its headings, then each fix's label and help. */
	namespace FixGroups
	{
		inline constexpr std::string_view Field = "Field";
		inline constexpr std::string_view Sound = "Sound";
		inline constexpr std::string_view Loading = "Loading saves";
		inline constexpr std::string_view Menus = "Menus";
		inline constexpr std::string_view Battle = "Battle";
		inline constexpr std::string_view WorldMap = "World map";
		inline constexpr std::string_view Arena = "Arena";
	}

	namespace Fix
	{
		inline constexpr FRow HpRecovery{"Event HP recovery", {"Events that heal the party", "restore HP. Original: some", "restore EP instead."}};
		inline constexpr FRow InstantFades{"Instant fades", {"Instant screen fades happen", "right away. Original: some", "leave the screen black."}};
		inline constexpr FRow SlowFades{"Slow fade-ins", {"Slow fade-ins from black play", "in full. Original: their", "start is cut off."}};
		inline constexpr FRow MovieMapChange{"Map change at movies", {"Map changes wait for their", "movie to finish. Original:", "they can stall."}};
		inline constexpr FRow GearAnimations{"Gear animations", {"Every part of a Gear", "animates. Original: some", "parts can freeze."}};
		inline constexpr FRow ScrollingTextures{"Scrolling textures", {"Scrolling textures move", "smoothly. Original: some", "jump when they loop."}};
		inline constexpr FRow EntryCamera{"Camera on map entry", {"A camera turned as a map", "starts stays put. Original:", "it swings from the old angle."}};
		inline constexpr FRow ScreenShakes{"Screen shakes", {"Screen shakes fade out over", "their own length. Original:", "some end early or late."}};
		inline constexpr FRow Ladders{"Ladders", {"Characters with no climbing", "animation keep their own.", "Original: garbled frames."}};
		inline constexpr FRow BledavikHotel{"Bledavik hotel", {"The hotel charges for the", "party you have. Original: a", "party of two pays for three."}};
		inline constexpr FRow RamsusShip{"Ramsus's ship", {"The ship's hum plays at its", "intended volume. Original:", "at full volume."}};
		inline constexpr FRow LacanMemories{"Lacan's memories", {"Room sounds fade out under", "the monologue. Original:", "they keep playing."}};
		inline constexpr FRow WidescreenCharacters{"Widescreen characters", {"Characters outside the 4:3", "area keep moving. Original:", "they stand still."}};
		inline constexpr FRow PartySprites{"Party sprites", {"Party members always use", "their own sprites. Original:", "pieces of others can show."}};
		inline constexpr FRow RpsHelp{"RPS help", {"Lose three times at Lahan's", "rock-paper-scissors and his", "wife offers to help."}};
		inline constexpr FRow ScenePictures{"Scene pictures", {"Painted backdrops stretch to", "fill screens up to 21:9.", "Original: black at the sides."}};
		inline constexpr FRow WidescreenStaging{"Widescreen staging", {"No one waits in view beside", "the 4:3 area for a cue.", "Original: some can be seen."}};
		inline constexpr FRow MoonGlow{"Moon glow", {"The glow around the moon", "fades into the night sky.", "Original: a dark square."}};
		inline constexpr FRow SwimStroke{"Swimming stroke", {"Bart keeps his fast stroke", "past the waterway's exits.", "Original: it can turn slow."}};
		inline constexpr FRow SilentSongs{"Silent song starts", {"A song started silently", "doesn't silence the next one.", "Original: it does."}};
		inline constexpr FRow SoundPanning{"Sound panning", {"Sounds panning across end", "where they should. Original:", "they can end elsewhere."}};
		inline constexpr FRow HighNotes{"High notes", {"Notes above the range play", "at the top. Original: they", "wrap around to low notes."}};
		inline constexpr FRow GearWeight{"Gear weight", {"After loading, Gear weight", "penalties use the save's", "data. Original: older data."}};
		inline constexpr FRow GearDeathblows{"Gear Deathblows", {"After loading, pilots keep", "the Deathblows of their", "Gear. Original: another's."}};
		inline constexpr FRow AgilityShown{"Agility display", {"Status shows agility over 16", "as 16, as battles use it.", "Original: the raw value."}};
		inline constexpr FRow DeathblowBar{"Deathblow bar", {"A Deathblow learned to 100%", "shows a full bar. Original:", "the bar shows empty."}};
		inline constexpr FRow BillyAmmo{"Billy's ammo", {"Unequipping ammo keeps its", "shots. Original: shots can", "be lost."}};
		inline constexpr FRow EtherHealing{"Ether healing", {"Ethers heal and cost the", "same in menus as in battle.", "Original: as a basic Ether."}};
		inline constexpr FRow FixFrameHp{"Fix Frame HP", {"Repairs the caster's own", "Gear. Original: checks the", "first member's Gear."}};
		inline constexpr FRow NamesEndingInS{"Names ending in s", {"Names keep their final s.", "Original: it is cut off, so", "Crescens shows as Crescen."}};
		inline constexpr FRow GearShopParts{"Gear shop parts", {"The Gear shop counts every", "part's bonus. Original: it", "skips some for a while."}};
		inline constexpr FRow GearShopPace{"Gear shop pace", {"The Gear in the shop turns", "at the normal pace. Original:", "three times faster."}};
		inline constexpr FRow SellingEquipped{"Selling equipped items", {"Selling worn equipment", "updates stats right away.", "Original: only later."}};
		inline constexpr FRow NewGameClock{"New game play time", {"A new game's play time starts", "at zero. Original: it counts", "from power-on."}};
		inline constexpr FRow InfinityChance{"Infinity chance", {"An undamaged Gear's Infinity", "chance is the same in every", "slot. Original: it varies."}};
		inline constexpr FRow ItemBuffs{"Item buffs", {"Item buffs last based on", "their user. Original: on the", "previous action."}};
		inline constexpr FRow EpAtLevel99{"EP at Ether level 99", {"EP keeps growing at Ether", "level 99. Original: it stops", "growing there."}};
		inline constexpr FRow EtherBuffs{"Ether buffs", {"Ether buffs work whatever", "the element. Original: some", "elements block them."}};
		inline constexpr FRow BuffsWearOff{"Buffs wear off", {"Atk, ACC and EV UP wear off", "after their turns. Original:", "they last the whole battle."}};
		inline constexpr FRow DefUpOnFoot{"Def UP on foot", {"Def UP raises defense by half", "on foot. Original: it does", "nothing on foot."}};
		inline constexpr FRow DefDownOnFoot{"Def DOWN on foot", {"Def DOWN lowers defense by a", "third on foot. Original: it", "does nothing on foot."}};
		inline constexpr FRow MissedStatusMoves{"Missed status moves", {"A status move that misses", "doesn't reset the status's", "duration. Original: it does."}};
		inline constexpr FRow ImmunityItems{"Igissol, Aegissol", {"Igissol and Aegissol grant", "their immunities. Original:", "they grant none."}};
		inline constexpr FRow GearElements{"Gear element changes", {"A Gear's element change wears", "off. Original: it stays, and", "clears other statuses."}};
		inline constexpr FRow GearStop{"Gear Stop", {"A stopped Gear's sure hits", "end with its Stop. Original:", "they last the whole battle."}};
		inline constexpr FRow GearPowerLoss{"Gear PWR loss", {"PWR loss lowers a Gear's", "attack until cured. Original:", "it does nothing."}};
		inline constexpr FRow LargeGearNumbers{"Large Gear numbers", {"Damage and healing on Gears", "past 65,535 stay correct.", "Original: they wrap around."}};
		inline constexpr FRow Counters{"Counters", {"A counter no longer stops a", "group attack or hits other", "enemies. Original: it can."}};
		inline constexpr FRow LastBossHp{"Last boss's HP", {"The final form has the HP its", "script gives it. Original:", "slightly less."}};
		inline constexpr FRow HiddenEnemies{"Hidden enemies", {"An enemy that reveals itself", "keeps its group working.", "Original: the group breaks."}};
		inline constexpr FRow WeaponTrails{"Weapon trails", {"Weapon trails blend with the", "scene. Original: they cover", "what is behind them."}};
		inline constexpr FRow SpellLighting{"Spell lighting", {"Spells with no lighting of", "their own use the stage's.", "Original: whatever was last."}};
		inline constexpr FRow StageColors{"Stage colors", {"Tints keep the stage's own", "colors. Original: flames can", "change color."}};
		inline constexpr FRow BattleEvents{"Battle events", {"Scripted battle events start", "every actor they name.", "Original: one can be skipped."}};
		inline constexpr FRow EventDefeats{"Event defeats", {"Losing a scripted battle", "counts as a defeat. Original:", "it can end otherwise."}};
		inline constexpr FRow Landmarks{"Landmarks after battle", {"Story landmarks stay after a", "battle. Original: hidden", "until you re-enter the area."}};
		inline constexpr FRow SeaBlastSound{"Sea blast sound", {"In the blast over the sea,", "the rushing sound plays too.", "Original: it is cut off."}};
		inline constexpr FRow MahanonFalls{"Mahanon's falls", {"The waterfalls get all their", "particles. Original: thinner", "falls."}};
		inline constexpr FRow OverlappingTrees{"Overlapping trees", {"Overlapping trees keep their", "order. Original: they swap as", "the camera moves."}};
		inline constexpr FRow CpuRangedMoves{"CPU ranged moves", {"The CPU picks ranged moves", "by their own strength.", "Original: by the next move's."}};
		inline constexpr FRow PracticeOpponents{"Practice opponents", {"Easy, Normal and Hard CPUs", "play at the level they name.", "Original: the menu's level."}};
		inline constexpr FRow PracticeFrameRate{"Practice frame rate", {"Practice runs at the frame", "rate its label shows.", "Original: slower than shown."}};
	}

	namespace Controls
	{
		inline constexpr std::string_view Device = "Device";
		inline constexpr std::string_view Keyboard = "Keyboard";
		inline constexpr std::string_view Controller = "Controller";
		inline constexpr std::string_view ButtonsHeading = "Buttons";
		inline constexpr std::string_view CameraHeading = "Camera";
		/** What a key or button is called on each device: in "{}" below and on the reset row. */
		inline constexpr std::string_view KeyNoun = "key";
		inline constexpr std::string_view ButtonNoun = "button";

		/** Each PlayStation button's row, named by what it does first, with what it does. */
		inline constexpr FRow Up{"Up", {"Moves up.", "", ""}};
		inline constexpr FRow Down{"Down", {"Moves down.", "", ""}};
		inline constexpr FRow Left{"Left", {"Moves left.", "", ""}};
		inline constexpr FRow Right{"Right", {"Moves right.", "", ""}};
		inline constexpr FRow Confirm{"Confirm", {"Confirm, talk, fierce attack.", "", ""}};
		inline constexpr FRow Cancel{"Cancel", {"Cancel, run or walk.", "", ""}};
		inline constexpr FRow Menu{"Menu", {"Opens the menu, strong attack.", "", ""}};
		inline constexpr FRow Jump{"Jump", {"Jump, weak attack.", "", ""}};
		inline constexpr FRow CameraLeft{"Camera left", {"Rotates camera, slows battle.", "", ""}};
		inline constexpr FRow CameraRight{"Camera right", {"Rotates camera, speeds battle.", "", ""}};
		inline constexpr FRow GearLeft{"Gear L", {"With Gear R: get on/off Gear.", "", ""}};
		inline constexpr FRow GearRight{"Gear R", {"With Gear L: get on/off Gear.", "", ""}};
		inline constexpr FRow Pause{"Pause", {"Pauses the game.", "", ""}};
		inline constexpr FRow Map{"Map", {"Shows the map.", "", ""}};
		/** The free camera's rows. */
		inline constexpr FRow Rotate{"Rotate", {"Rotates the camera.", "", ""}};
		inline constexpr FRow Tilt{"Tilt", {"Tilts the camera up/down.", "", ""}};
		inline constexpr FRow Zoom{"Zoom", {"Zooms the camera in/out.", "", ""}};
		inline constexpr FRow ResetCamera{"Reset camera", {"Resets zoom and rotation.", "", ""}};
		/** A key row's second line: the rows that clear and swap, by their labels. */
		inline constexpr std::string_view ClearAndSwap = "{} to clear, {} to swap.";

		/** The help's "{}": the device's noun, then the Cancel row's label. */
		inline constexpr FRow AlwaysRun{"Always run", {"Run without holding a {}.", "Hold {} to walk.", ""}};
		/** The help's "{}": the Confirm row's label. */
		inline constexpr FRow WalkIntoDoors{"Walk into doors", {"Doors open as you walk into", "them. When off, press {}.", ""}};
		inline constexpr FRow OriginalButtonIcons{"Original PSX icons", {"Keep the original PSX icons.", "Your bindings still apply.", ""}};
		inline constexpr FRow MouseSensitivity{"Mouse sensitivity", {"How fast the mouse rotates", "the camera.", ""}};
		inline constexpr FRow StickSensitivity{"Stick sensitivity", {"How fast the stick rotates", "the camera.", ""}};
		/** The reset row's label, and what it says when done, for the keyboard and for a controller. */
		inline constexpr std::string_view ResetKeys = "Reset keys";
		inline constexpr std::string_view ResetButtons = "Reset buttons";
		/** The help's "{}": the device's noun. */
		inline constexpr std::array<std::string_view, 3> ResetHelp{"Restores the default {}s.", "", ""};
		inline constexpr std::string_view KeysReset = "Default keys restored.";
		inline constexpr std::string_view ButtonsReset = "Default buttons restored.";

		/** Listening for a key: what to press ("{}" the noun), then how to stop. */
		inline constexpr std::string_view PressKey = "Press a {} to assign.";
		inline constexpr std::string_view StopListening = "Esc or wait to cancel.";
		/** Listening for an axis: the way asked for, on the mouse and on a stick. */
		inline constexpr std::string_view MoveMouseRight = "Move the mouse right.";
		inline constexpr std::string_view MoveMouseUp = "Move the mouse up.";
		inline constexpr std::string_view TurnWheelUp = "Turn the wheel up.";
		inline constexpr std::string_view PushStickRight = "Push a stick right.";
		inline constexpr std::string_view PushStickUp = "Push a stick up.";
		inline constexpr std::string_view PushStickDown = "Push a stick down.";

		/** What a key action told: "{}" the noun, or a row's label then the noun. */
		inline constexpr std::string_view OnlyKey = "{}'s only {}.";
		inline constexpr std::string_view CannotClear = "Can't clear the only {}.";
		inline constexpr std::string_view Inverted = "Inverted.";
	}

	namespace Saves
	{
		inline constexpr FRow Import{"Import saves", {"Copy saves from a memory", "card image or save file into", "one of your cards."}};
		inline constexpr FRow Export{"Export card", {"Saves a card as a memory card", "image for emulators, in the", "Cards folder."}};
		inline constexpr FRow DocumentExport{"Export card", {"Save a memory card image", "using the system file picker.", "Choose where it is kept."}};
		inline constexpr std::array<std::string_view, 3> DocumentPick{"Choose a memory card image", "or save file in the picker.", ""};
		inline constexpr std::array<std::string_view, 3> DocumentDestination{"Choose where to save the", "memory card image.", ""};
		inline constexpr std::array<std::string_view, 3> DocumentUnavailable{"Couldn't open the file picker.", "Please try again.", ""};
		inline constexpr std::array<std::string_view, 3> DocumentCancelled{"Cancelled. Your game saves", "are unchanged.", ""};
		inline constexpr std::array<std::string_view, 3> DocumentTooLarge{"That file is too large.", "Choose a memory card image", "or save file up to 1 MiB."};
		inline constexpr std::array<std::string_view, 3> DocumentExported{"Memory card image exported.", "Your game saves are kept.", ""};
		/** A card's name: its number. */
		inline constexpr std::string_view Card = "Card {}";

		/** The file browser: the folder above, a folder or drive's value, and a folder with nothing to show. */
		inline constexpr FRow Up{"..", {"Up one folder.", "", ""}};
		inline constexpr std::string_view Folder = "Folder";
		inline constexpr std::string_view Drive = "Drive";
		inline constexpr std::string_view NoFolders = "No folders";

		/** The import: the card the saves go into, the copy (its "{}" how many), and each save picked. */
		inline constexpr FRow Into{"Into", {"The card to copy the saves", "into. Each keeps its slot", "number."}};
		inline constexpr FRow Copy{"Copy {}", {"Press Confirm to copy.", "", ""}};
		inline constexpr std::string_view OneSave = "1 save";
		inline constexpr std::string_view ManySaves = "{} saves";
		/** The copy's value and help when it would overwrite saves: how many, then the card; the saves' numbers fill the second line. */
		inline constexpr std::string_view Overwrites = "Overwrites {}";
		inline constexpr std::array<std::string_view, 3> OverwriteHelp{"It would overwrite in {}:", "", "Press Confirm twice to copy."};
		/** The same after the first Confirm: the saves' numbers fill the second line, the card is the third's "{}". */
		inline constexpr std::array<std::string_view, 3> OverwriteAsked{"Confirm again to overwrite", "", "in {}. Cancel keeps them."};
		/** A save's help: its number and its play time. */
		inline constexpr std::array<std::string_view, 3> SaveHelp{"Slot {}, play time", "{}.", ""};

		/** What the pages tell after an action: "{}" a count of saves, then a card. */
		inline constexpr std::array<std::string_view, 3> Unreadable{"Couldn't read the file.", "", ""};
		inline constexpr std::array<std::string_view, 3> NotACard{"Not a memory card image", "or save file.", ""};
		inline constexpr std::array<std::string_view, 3> NoSaves{"No Xenogears saves found.", "", ""};
		inline constexpr std::array<std::string_view, 3> CopyFailed{"A save couldn't be copied.", "", ""};
		inline constexpr std::array<std::string_view, 3> Copied{"Copied {} into", "{}.", ""};
		inline constexpr std::array<std::string_view, 3> DamagedCard{"A save on the card is", "damaged. Nothing was written.", ""};
		inline constexpr std::array<std::string_view, 3> WriteFailed{"Couldn't write the card", "image.", ""};
		inline constexpr std::array<std::string_view, 3> Exported{"Saved as {}.mcr in", "the Cards folder.", ""};
	}

	/** The port's words about the player's discs: under the game's disc prompts ("INSERT DISC 2"), and the screen shown without disc 1. */
	namespace Discs
	{
		/** The disc asked for is still not where the game looks for it. */
		inline constexpr std::string_view NotFound = "Not found in Games.";

		/** The screen shown while disc 1 is missing: what is wrong, where the discs go ("{}" the folder), what was found and what not. */
		inline constexpr std::string_view FirstMissing = "Xenogears' first disc was not found.";
		inline constexpr std::string_view WhereTheyGo = "Put your disc images of the USA release (BIN/CUE, CHD, ISO or IMG) in this folder:\n{}\n"
														"The game starts as soon as disc 1 is there.";
		/** A disc found, "{}" its number then its file. */
		inline constexpr std::string_view Found = "Found disc {}: {}";
		inline constexpr std::string_view NotUsed = "Not used:";
		/** A file not used, "{}" its name then why. */
		inline constexpr std::string_view NotUsedFile = "{}: {}";
		/** After the files listed, "{}" how many more there are. */
		inline constexpr std::string_view More = "And {} more.";
	}

	/**
	 * The question asked after the second disc's prompt, where the port has the expanded second disc: its line, its
	 * two answers (the left one, the original's, then the right one, the expanded second disc's, where the pointer
	 * starts), and what each means, shown while the pointer is on it.
	 */
	namespace SecondDisc
	{
		inline constexpr std::string_view Question = "How should Disc 2 be told?";
		inline constexpr std::string_view Original = "Original";
		inline constexpr std::string_view Extended = "Extended";
		inline constexpr std::array<std::string_view, 3> OriginalHelp{"Disc 2 as the original game", "tells it, much of it through", "narration."};
		inline constexpr std::array<std::string_view, 3> ExtendedHelp{"What the original only", "narrates is played: new", "scenes, places and battles."};

		/** The Fixes page's row in a game that tells it extended: Confirm twice tells the rest as the original, for good. */
		inline constexpr FRow Row{"Disc 2", {"The rest of Disc 2 played as", "new scenes. Confirm to tell", "it as the original instead."}};
		/** Its help once Confirm was pressed, and once the game was switched where it stays. */
		inline constexpr std::array<std::string_view, 3> SwitchAsked{"Confirm again to switch. It", "can't be switched back to", "Extended afterwards."};
		inline constexpr std::array<std::string_view, 3> Switched{"Disc 2 is told as the", "original from here on.", ""};
	}

	/** The screen shown over the game's last picture when it stops, or in its place when it cannot start: what happened, then why. */
	namespace Stop
	{
		inline constexpr std::string_view Stopped = "The game has stopped.";
		inline constexpr std::string_view CannotStart = "The game cannot start.";
		/** Where the game's log is, "{}" its file. */
		inline constexpr std::string_view Log = "What the game was doing is in its log, this file:\n{}\n"
												"Please include it in a report. When the game starts again, a copy of it named with the date stays in the "
												"same folder for some days.";
		inline constexpr std::string_view SavesKept = "Your saves are kept: close the game and start it again to go on from your last save.";
		/** The last line, "{}" the game's version. */
		inline constexpr std::string_view Version = "Weltall {}";
	}

	/** Text with each "{}" replaced by the next of the values from Next, which moves past them; one past End stays "{}". */
	inline std::string FillFrom(std::string_view Text, const std::string_view*& Next, const std::string_view* End)
	{
		std::string Filled;
		for (std::size_t At = 0; At < Text.size(); ++At)
		{
			if (Text.substr(At, 2) == "{}" && Next != End)
			{
				Filled += *Next++;
				++At;
				continue;
			}
			Filled += Text[At];
		}
		return Filled;
	}

	/** Text with each "{}" replaced by the next of Values, in order. */
	inline std::string Fill(std::string_view Text, std::initializer_list<std::string_view> Values)
	{
		const std::string_view* Next = Values.begin();
		return FillFrom(Text, Next, Values.end());
	}

	/** A help's lines with each "{}" replaced by the next of Values, the first line's first. */
	inline std::array<std::string, 3> Fill(const std::array<std::string_view, 3>& Lines, std::initializer_list<std::string_view> Values = {})
	{
		const std::string_view* Next = Values.begin();
		std::array<std::string, 3> Filled;
		for (std::size_t Line = 0; Line < Lines.size(); ++Line)
		{
			Filled[Line] = FillFrom(Lines[Line], Next, Values.end());
		}
		return Filled;
	}
}
