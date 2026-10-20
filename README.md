# Weltall

Weltall is a native port of *Xenogears* (PlayStation, 1998), rebuilt in Unreal Engine 5. The goal is to
make the game feel like a modern PC release while keeping the original gameplay intact.

This fork extends [Roman215's Weltall](https://github.com/Roman215/Weltall) with an **experimental
Android port** for ARM64 phones, including configurable on-screen controls and hardware controller
support. It stays in landscape and rotates between both landscape directions. Android testing is
in progress on a Moto G Stylus 2025 with a Bluetooth Stadia controller; complete both-disc playthrough
and broader controller/lifecycle testing remain unfinished. See the
[Android validation record](Docs/AndroidGameplayValidation.md) for current coverage.

You'll need your own copy of the USA release or a supported English undub. Weltall doesn't include
any game data in this repository. For desktop builds, put your disc images in the `Games` folder and
the game will find them automatically. The Android build instructions below describe how to bundle
your own local copies of both USA discs into a private APK.
BIN/CUE and CHD preserve movie audio; ISO/IMG also work, but the movies will be silent. Convert PBP,
ECM or other formats to BIN/CUE first. You can add Disc 2 while the game is running when it asks for
it. You can also start with only Disc 2, as on the PlayStation, to load your Disc 2 saves.

The undub plays with English text and Japanese voices, and its subtitles appear in the movies. If you
have both versions, choose which voices you hear under **Options → Sound → Voices**. **Options → Sound →
Subtitles** can also show the undub's subtitles with the English voices, or turn them off. Japanese
retail discs aren't playable, but you can dump their files (see [the modding guide](Docs/Modding.md)).

**Upstream desktop status:** Both discs are playable, including field exploration, character and Gear
battles, the world map, menus, the Battling arena and movies. Gameplay behavior has been checked
against the original game's code. The upstream developer reports an ongoing playthrough with over
18 hours played without a hang. Automated tests have run every map and story battle, along with long
stretches of the story from save to save. The desktop port aims for start-to-finish play, with reported
issues still being fixed. If something goes wrong, the game displays an error and tells you which log
to include in a bug report.

## What Weltall adds

- **Higher resolutions and widescreen** up to 32:9, with scenery extending beyond the original 4:3 view.
- **Smooth motion at any refresh rate**, including 120 Hz and above, without changing the game's pace.
- **A free camera** on the field and world map, with rotation, tilt and zoom using a mouse or right stick.
- **Per-pixel depth sorting** to fix characters, Gears and scenery clipping or flickering over each other.
- **Graphics options** for sharper or smoother scenery and sprites, anti-aliasing, upscaled movies and a CRT filter.
- **Softer battle flashes**, or none at all, for the bright screen flashes from attacks and Ether.
- **3D Gears** on the field and world map, using their battle models, plus full-size party sprites in Gear areas.
- **Battle speeds of 2x and 4x**, and faster text up to instant. Timed dialogue still leaves you time to read it.
- **Balance options** to earn up to four times the experience and gold, and to have fewer random battles, more of
  them, or none at all.
- **Chapter titles** displayed when each chapter begins.
- **Custom party order**, so any character can lead the party (see below).
- **Always run**, and doors that open when you walk into them.
- **Keyboard, mouse and controller support**, with rebinding and controller hot-plugging. Button prompts match
  your current bindings.
- **Save options** to load from the menu anywhere, import PlayStation memory-card or emulator saves, and export
  memory-card images.
- **Fixes for bugs in the original game**.
- **Modding options** to export the game's textures as PNG, its music as WAV and its text and font to edit, dump
  the disc's files, and load your replacements from the `Assets` folder, including texture packs made for
  RetroArch's Beetle PSX HW core. You can also use parts of the fan mod Perfect Works Build from your own copy. See
  [the modding guide](Docs/Modding.md) for the details.

You can adjust these in Options from the title screen or in-game menu, including switching back to the
original behavior.

The source also includes tools for turning Disc 2's narrated sections into playable sequences, with
two examples. These are separate from the main port and aren't included in the downloadable builds.

Weltall is not affiliated with or endorsed by the owners of *Xenogears*. All game content and
trademarks belong to their respective owners.

## Playing on Windows or Linux

Download the build for your system and unzip it wherever you want. Put the USA or undub disc images in
the `Games` folder (subfolders work too), then run `Weltall.exe` on Windows or `Weltall.sh` on Linux.
Windows requires 64-bit Windows 10 or 11 and a DirectX 12 graphics card. Linux requires a 64-bit
system with a Vulkan graphics driver. If neither disc is there, the game shows where it looked and what
it found, then starts once you add one. If you start with only Disc 2, the game asks for Disc 1 when you
choose New Game or load a Disc 1 save, as the original does.

Players have run the Linux build on a Steam Deck and on CachyOS with an AMD graphics card.

Options covers display, sound, camera, text speed, bug fixes, controls, and a Modding page with the
balance options and the Assets page. During battles, R1 increases the speed from 1x to 2x to 4x, and
L1 decreases it.

To keep the original PlayStation button symbols in prompts and menus, turn on **Original PSX icons**
under **Options → Controls**. Your bindings still apply, and the rebinding rows show your actual keys.

To rearrange the party, open the menu and press Right from the command list. Select a character with
×, then select another to swap them. Press △ to restore the story's order. The first character leads
on the field and world map, and your order also applies in menus and ordinary battles.

Your order is saved for that group of characters. If someone joins or leaves during the story, the
party returns to the story's order; when the same group is together again, your order returns. In
Member Change, a replacement takes the outgoing character's position. Cutscenes, scripted battles,
and field scenes where party members are speaking or placed around the map use the story's order.

### Default controls

| PlayStation | Keyboard | Gamepad |
|---|---|---|
| Directions | W A S D, arrow keys | D-pad, left stick |
| × | E, Enter | A |
| ○ | Q, Escape | B |
| □ | F | X |
| △ | Space | Y |
| L1, R1 | Tab, R | LB, RB |
| L2, R2 | 1, 4 | LT, RT |
| Start, Select | P, Backspace | Menu, View |
| Turn the view | Mouse | Right stick |
| Zoom | Mouse wheel | Right stick up and down |
| Reset the view | Middle mouse button | Right stick click |

You can rebind every control on the Controls page. Wherever the game mentions a PlayStation button,
it shows the key or controller button you've assigned to it.

On Android, tap **Controls** to adjust the on-screen buttons' idle and pressed opacity, size and
positions, or reserve side areas so they cover less of the game. Touch controls can be used alongside
a controller. The controller bindings in the table above also apply to Android.

### Saves and settings

On Windows, settings and saves are in `%LOCALAPPDATA%\Weltall`. On Linux, settings are in
`~/.config/Weltall` and saves are in `~/.local/share/Weltall/Saves`.

Use the Saves page in Options to import a PlayStation memory-card image or an individual save file,
including emulator saves. You can also export a card as an `.mcr` image to `Saves\Cards`.

### Reporting a problem

If the game stops with an error on screen, attach the log file it names to a bug report and describe
what you were doing. If it crashes, zip and attach the newest crash folder:

- Windows: `%LOCALAPPDATA%\Weltall\Saved\Crashes`
- Linux: `~/.config/Epic/Weltall/Saved/Crashes`

That folder includes the log and crash dump. A save from just before the problem is helpful too.

## Development

The core (`Source/WeltallCore`) has no Unreal dependencies, so you can build and run its tests
separately. You'll need CMake 3.25+ and a C++20 compiler:

```sh
cd Tests
cmake --preset windows        # or: linux, linux-clang
cmake --build --preset windows
ctest --preset windows
```

Tests that need game data look for disc images in `Games/` and report a skip if a disc is missing.
Oracle tests compare the port's results with stored digests from the original code, run in an
emulated CPU with inputs from the same generator used by the tests.

Story additions are plain text files in `Content/Story`, loaded at startup. Each folder is a pack
and each `.txt` file is a document. They can patch field scripts and messages, create maps from
existing map assets, change battle narration, add world map locations and define subchapter titles.
Scripts use the original game's instruction names and message markup. Each document references
the original content it needs and contains only the additions.

The `disc2` pack includes two playable examples: the rematch above the forest, and Elly's trip to the
Mass Driver with a short dungeon. Both rejoin the original story afterward. The pack is only used if
the player selects it when Disc 2 begins. [Docs/Story](Docs/Story/README.md) explains how to write these
documents using the existing files as examples.

`WeltallMapText` is built alongside the tests and prints a disc map in this text format, for example
`WeltallMapText 613`. Use it to inspect the scripts you're patching. Its output contains original game
scripts and text, so don't commit it. The tests check each document against its map or battle.

### Packaging

`Scripts/package.py` builds standalone packages from a commit. It needs an Unreal Engine 5.8 source
build, with `UE_ROOT` set in the environment or in a `local.env` file at the repository root:

```sh
python Scripts/package.py                     # Development
python Scripts/package.py DebugGame Shipping  # any of DebugGame, Development, Shipping
python Scripts/package.py --platform Linux    # for Linux, with the engine's toolchain in LINUX_MULTIARCH_ROOT
```

Packages are written to `out/builds/<Configuration>/<Platform>`. Run `Weltall.exe` or `Weltall.sh`
from there. The script links your checkout's disc images into the package's `Games` folder. All
builds share the same settings and saves in your user folders.

`python Scripts/package.py --archive Shipping` also creates a release zip in `out/releases`, without
disc images, and a separate debug symbols archive.

`python Scripts/release.py 0.8.2` builds both Shipping packages from a pushed commit whose
`ProjectVersion` matches that version. It creates the archives and a GitHub draft release with the
player downloads and release notes, using the GitHub CLI. Publish the draft from the release page.

Packages leave out the `disc2` pack and play the original Disc 2 without asking you to choose a mode.
Build with `--expansion` to include the examples; those archives are marked in the filename.

### Building the experimental Android port

The tested build host is Windows with Git, Python 3.10 or newer, Visual Studio 2022 C++ build tools,
the Windows SDK and .NET Framework 4.8 SDK. Use an **Unreal Engine 5.8 source build** with its Android
dependencies installed. This branch has been built with UE 5.8.4, Android SDK 36/build tools 36.0.0,
NDK r27c and JDK 21. Android packaging uses `Scripts/android_package.py`, separately from the desktop
packager above. See [AndroidBuild.md](Docs/AndroidBuild.md) for additional setup and test details.

1. Check out the `android-port` branch and run the following commands from the repository root.
   Commit any source changes you want to build: the packager exports committed `HEAD`, not uncommitted
   edits. You can select another commit or branch with `--revision`.
2. Set your tool locations in environment variables or create an ignored `local.env` in the repository
   root. Replace these example paths with your actual installations; do not put quotes around values
   in this file. Existing environment variables take precedence.

   ```text
   UE_ROOT=C:/dev/UnrealEngine-5.8
   JAVA_HOME=C:/Android/jdk-21
   ANDROID_HOME=C:/Android/sdk
   ANDROID_SDK_ROOT=C:/Android/sdk
   NDKROOT=C:/Android/sdk/ndk/27.2.12479018
   ```

   ```sh
   python Scripts/android_engine_fix.py --apply
   python Scripts/android_package.py --check-toolchain
   ```

   The first command corrects UE 5.8's Android volume-callback clock in your source engine installation;
   see the [build guide](Docs/AndroidBuild.md#prerequisites) for details. The second checks the engine installation;
   Unreal performs the Android SDK/toolchain checks during the
   build. Complete its Android dependency setup and accept the SDK licenses before packaging.
3. Put both USA discs' **BIN/CUE files** in a directory outside the checkout, such as
   `D:/Game Discs/Xenogears` or `../bin`. The selected directory must contain exactly two CUE sheets,
   with every referenced BIN track inside that directory. Subfolders are supported. Keep filenames
   consistent with the CUE references, including case. This Android bundling command expects BIN/CUE;
   the desktop runtime's other supported formats do not apply to this step.

   Specify that directory with **`--include-discs`**. Quote paths containing spaces:

   ```sh
   python Scripts/android_package.py --include-discs "D:/Game Discs/Xenogears" --print-plan
   python Scripts/android_package.py --include-discs "D:/Game Discs/Xenogears" --configuration Development
   ```

   For the sibling `bin` folder instead:

   ```sh
   python Scripts/android_package.py --include-discs ../bin --configuration Development
   ```

   `--print-plan` validates the input files and shows the output locations without starting a build.
   The directory is a **build-time input on your computer**: the script bundles the discs into the APK,
   and the phone prepares its private working copy at first launch. It does not access the computer's
   directory while playing. Omitting `--include-discs` builds a runtime without bundled game data.
4. Find the APK in the new `out/android/packages/Development/<build-id>/` directory; the script prints
   its exact filename. Build logs are in `out/android/logs/`. Enable USB debugging on the phone, approve
   the computer, and install with Android SDK platform-tools:

   ```sh
   adb devices
   adb install -r "out/android/packages/Development/<build-id>/Weltall-arm64.apk"
   ```

   Replace `<build-id>` with the generated directory name. Open Weltall and let disc preparation finish.
   For updates, keep the same signing key and use `install -r` to preserve app data; do not uninstall
   the app to update it.

The exported project, bundled discs, APKs, logs and test outputs stay in ignored `out/` directories.
Disc images, saves and game-derived captures must stay out of Git and public build artifacts. Keep
an APK containing your disc data local. A successful build does not establish Android gameplay
readiness; follow [the validation matrix](Docs/AndroidGameplayValidation.md) when testing changes.

## Credits

Four of Weltall's bug fixes come from the fan mod
[Perfect Works Build](https://github.com/PWBuild-Team/Perfect_Works_Build). When you use your own copy of the mod,
Weltall also recreates the mod's changes to the game's code: the battle dialogue windows, the rebalanced party and
items, and the Battling arena modes. All of this is done with the Perfect Works Build team's permission.

## License

Weltall's code is under the MIT License; see `LICENSE`. Third-party code keeps its own license, listed
in `ThirdPartyNotices.md`. This license applies only to Weltall, not to *Xenogears* or its game content
and trademarks.

## Support

If you'd like to support my open-source work, you can do so through
[GitHub Sponsors](https://github.com/sponsors/Roman215) or [Ko-fi](https://ko-fi.com/roman215).
