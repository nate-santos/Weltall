# Weltall Android port: source review and implementation plan

Reviewed 17 October 2026, upstream commit `7e81074bcdb385eb0300450a5023a7f2a21058ec` (0.8.1).
Fork: https://github.com/nate-santos/Weltall

Updated requirements: both USA discs are supplied locally as BIN/CUE pairs in `../bin/`. The game must remain horizontal and rotate between both landscape directions as the phone is turned. Touch controls must support transparency and minimize obstruction. Development is on the `android-port` branch and source changes are pushed to the fork as they pass review. The first device is a Moto G Stylus 2025 connected over USB (Android 16, ARM64, Adreno).

The requested local APK will include both discs. Source history, remote repository, CI and public artifacts remain free of game data. `Scripts/android_package.py --include-discs ../bin` stages discs only into an ignored exported project and packages them locally. Without that flag, it builds a data-free runtime. A bundled-disc APK stays local and is never uploaded by CI or committed. This local bundle supersedes the original import-only APK plan below.

## Finding

Weltall is a native C++ implementation of Xenogears with an Unreal presentation layer. It is not a general PlayStation emulator running the original CPU program. Calling it a full automated recompilation would also be misleading: the inspected source contains structured, game-specific implementations of the field, battles, world map, menus, and game loop. This review establishes the runtime architecture, not how the author originally produced every function.

There is still hardware behavior reproduced in software: `FSpu` implements the PS1 sound processor, graphics code preserves GTE arithmetic behavior, and shaders reproduce VRAM/palette and blending rules. Game event/sprite scripts are interpreted; these are game scripting languages, not a general MIPS CPU interpreter. README's emulated-CPU reference describes how original-code oracle digests were produced for testing.

The disc supplies original assets, scripts, audio, movies, and executable-resident data. Reading a PS-X executable for data does not imply executing it as MIPS instructions. Both user-supplied BIN/CUE pairs are present in `../bin`, outside the `Weltall` Git checkout. The native core identifies both USA discs on the Moto, and disc-structure/movie-decoder checks pass. See `Docs/AndroidValidation.md` for the measured coverage.

## Evidence and likely change locations

| Component | Source | Android implication |
|---|---|---|
| Native game execution | `Source/WeltallCore/Private/Game/GameLoop.cpp`, `Public/Game`, `Public/Battle`, `Public/Field`, `Public/WorldMap` | Reuse gameplay code; compile for ARM64. |
| Engine-independent C++20 core | `Source/WeltallCore/WeltallCore.Build.cs`, `Tests/CMakeLists.txt` | Can validate core without Unreal; Android test execution needs a device runner. |
| Sound hardware model | `Source/WeltallCore/Public/Sound/Spu.h` | Reuse 24-voice, 44.1 kHz synthesis; validate mobile audio/resampling. |
| Unreal procedural audio | `Source/Weltall/Private/Audio/GameSoundWave.cpp` | Check underruns, audio focus, Bluetooth, pause/resume. |
| Custom rendering | `Source/Weltall/Private/Rendering`, `Shaders/Private`, `Private/Presentation/WeltallViewportClient.cpp` | Highest technical uncertainty: shader cooking and mobile Vulkan behavior. |
| Desktop graphics configuration | `Config/DefaultEngine.ini` | Windows DX12 SM6 and Linux Vulkan SM6 currently; no Android configuration. Desktop Vulkan success is not proof of mobile compatibility. |
| Disc discovery | `Source/Weltall/Private/Shell/GameDiscsSubsystem.cpp` | Replace executable-adjacent Games directory with Android import/storage handling. |
| Saves/settings | `Source/Weltall/Private/Shell/PlayerFolders.cpp` | Android currently falls into XDG desktop fallback; add explicit app-storage locations. |
| Controls | `Source/Weltall/Private/Input`, `Private/Presentation/WeltallViewportClient.cpp` | Reuse logical pad mapping, add multi-touch overlay and mobile-safe navigation. |
| Build/package | `Weltall.uproject`, `Scripts/package.py` | UE 5.8; packaging currently supports Windows/Linux only. Add Android path. |

Unreal's normal scene renderer is bypassed. Nanite, Lumen and many heavyweight scene features are already disabled. This is encouraging, but generic advice to disable Lumen will not solve this port: custom render passes, integer textures, structured buffers, blend modes, depth/stencil, MSAA and resolve behavior need testing.

## Proposed initial target

Engineering target, not a measured minimum: ARM64, Android 12 or newer, 6–8 GB RAM, Vulkan-capable GPU, landscape orientation. Start on one recent midrange device, then validate both Adreno and Mali/Immortalis GPU families. Choose OS minimum and SDK/NDK versions compatible with the actual UE 5.8 checkout; pin versions rather than independently installing the newest tools.

Start with roughly 720p internal rendering, a 60 Hz presentation target and an optional 30 Hz battery mode. Preserve the core's simulation timing separately from presentation rate. Disable supersampling and expensive upscaling initially. Measure sustained performance over at least 30 minutes; do not promise 60 fps before profiling.

## Landscape orientation and touch-control design

Use sensor-driven landscape orientation: normal landscape and reverse landscape, never portrait. Recalculate viewport placement, safe insets and control positions after a 180-degree flip, including phones with a cutout on either end. Release all captured touches during orientation/surface changes so movement and buttons cannot stick. Test this behavior on real devices, including Android's rotation-lock setting; offer an in-game landscape-side lock as an optional preference.

The proposed controls will be original project C++ code using Unreal's existing Slate touch/widget APIs, integrated with Weltall's input layer. No third-party emulator overlay or external control package is needed. Slate and Enhanced Input are already dependencies of the project. Epic's `SVirtualJoystick` offers touch handling and active/inactive opacity controls, but the full D-pad/button layout needs project-specific integration. Prototype the built-in widget where useful; implement custom Slate controls when needed for D-pad precision, layout editing and simultaneous button presses. Use original procedural outlines and text labels, not extracted game artwork or copied controller skins. Keep engine source in the installed engine, not in this repository.

Proposed implementation locations: new `Source/Weltall/Private/Input/TouchControls` code, existing `WeltallPlayerController` and input mapping, `Private/Presentation/WeltallViewportClient.cpp`, and player-settings persistence. Verify the custom viewport actually composites the Slate overlay; do not assume the engine's default joystick automatically appears over this renderer. Combine touch and hardware input per source so lifting one finger cannot release a button still held on a controller.

Controls will support adjustable transparency. Initial defaults to test: 25% opacity while idle, 55% while pressed, with separate user-adjustable sliders and immediate visual feedback. The Controls shortcut's label and outline follow these same idle/pressed settings. Transparent backgrounds and thin outlines keep the scene visible; touch targets remain comfortably large even when the visible marks are small. Opacity changes must not disable touch detection, including the fixed shortcut target at zero opacity. Keep the appearance panel readable and the layout editor's Done action visible while editing so controls can be recovered after selecting very low opacity.

Default layout: D-pad near the lower left edge, face buttons near the lower right, compact shoulder controls along the upper side edges, and small Start/Select/menu controls outside central dialogue/action areas. Allow moving/scaling controls and saving layouts. Use a tap-friendly D-pad with diagonals; camera gestures operate only outside captured control regions. Never intercept unrelated screen touches with a full-screen invisible button.

Offer two layouts: (1) minimal transparent edge overlay with widescreen play; (2) an unobstructed picture mode that reserves side areas for controls, reducing picture width as necessary while preserving aspect ratio. On wider phones use spare side space first. Fade controls during movies or inactive cutscenes while retaining a discoverable way to confirm/skip when supported; restore immediately when input is needed. Hide the overlay during controller use, with a touch-accessible way to restore it. Keep all required PS1 buttons accessible, including shoulders used for camera and battle speed.

Acceptance: readable dialogue and battle commands, no required control under cutouts/navigation gestures, movement plus two simultaneous action touches, both landscape sides, and no stuck inputs after rotation, backgrounding, controller disconnection or touch cancellation.

## Repository and local-data boundaries

The repository contains original implementation code, documentation, test source and synthetic fixtures. Original disc data, extracted game assets/scripts/text/audio, saves, gameplay captures, generated test results and packaged binaries stay local. Existing licensed project dependencies and required notices remain; the requirement is to exclude original game content and generated artifacts, not to claim that source code has no intellectual property.

| Local location | Purpose | Git treatment |
|---|---|---|
| `../bin/` | Supplied Disc 1/2 BIN/CUE pairs | Outside the checkout; enclosing workspace also ignores `/bin/`. |
| `Games/` | Optional local copies/link for readers that require this path | Ignored; never commit or package. |
| `out/`, `build/`, `output/` | Builds, APK/AAB packages, engine cook/stage products | Ignored, including nested directories. |
| `testing/`, `test-results/`, `logs/` | Test runs, logs, private comparisons and local validation records | Ignored. |
| `captures/`, `dumps/`, `Assets/`, `Saved/` | Game-derived captures, exports, caches and saves | Ignored. |
| `Tests/` | Authored test source and independently generated fixtures | Tracked source; do not confuse it with `testing/`. |

The checkout ignore rules also exclude BIN/CUE/CHD/ISO/IMG/PBP/ECM files anywhere, common save-image extensions, and APK/AAB packages. Do not move outputs to arbitrarily named directories: route all generated data through the ignored locations above. Do not force-add ignored files. `.gitignore` prevents normal accidental additions but cannot stop force-add or protect a file that was already tracked.

Implemented: `Scripts/repo_guard.py`, local pre-commit/pre-push hooks, and CI validation reject staged/tracked disc extensions and generated-data paths. The guard reads Git blobs rather than the working tree, and the pre-push hook checks outgoing history including files later removed. Synthetic tests cover uppercase extensions, nested output folders, forced additions and recognizable renamed disc/extracted-data signatures. Authored `Tests/` source remains trackable. CI uses synthetic fixtures only: no ROM uploads, game-data caches, gameplay screenshots, or private save artifacts. Local disc-dependent runs write results to ignored directories. CI is a backstop, not a substitute for checking before pushing, since a failed CI run occurs after upload.

Before each commit/push: inspect the staged file list and diff, run the repository-data guard, and confirm no game-derived content is embedded in otherwise ordinary text/image files. Generated snippets from original scripts and text must not become fixtures or documentation. Do not upload local gameplay logs/screenshots automatically. For data-free packaging, verify the APK/AAB contains no disc data; for the requested local bundled APK, allow only the selected two validated CUE/BIN pairs and their integrity manifest. Both modes exclude saves, captures and test artifacts. Clean code builds should succeed from a checkout without discs; gameplay tests use local data separately. The guard cannot establish provenance of arbitrary text or encoded data, so review new assets before staging.

## Milestones

### 1. Establish repeatable baseline (2–5 working days)

- Create an Android development branch from the reviewed revision, keeping upstream tracking.
- Validate both local CUE descriptors and their track files, then load each through the project's disc reader to verify supported edition, sector layout and integrity errors. Keep validation logs in `testing/`; do not infer validity from filenames alone.
- Point desktop runtime at `../bin/` using its existing `-Games=` option. Existing tests default to `Games/`: add a configurable local test-data directory, or use an ignored local Games directory/link. Do not copy disc contents into tracked fixtures.
- Implement the repository-data guard described above before adding implementation commits; validate ignore coverage and ensure no disc/output paths are tracked.
- Install/configure the UE 5.8 source build and its matching Android toolchain using Epic's setup guidance.
- Run existing desktop core tests including disc-dependent cases with both supplied discs. Record passed, failed and skipped tests; investigate unexpected missing-disc skips. Establish a PC gameplay reference using these same images before changing platform code.
- Build a small empty ARM64 Unreal Android application on the selected phone to isolate toolchain issues from Weltall issues.
- Cross-compile WeltallCore with NDK Clang. Audit alignment, signed overflow assumptions, filesystem conversions and binary layouts as failures arise.
- For device-side core tests, provide a dedicated runner: the existing Catch2/CTest discovery flow expects to execute test binaries and cannot simply execute Android binaries on Windows.

Exit: validated input discs, recorded desktop baseline, repository-data guard, clean ARM64 core build and executable synthetic test subset, plus an installable empty Unreal app. Run disc-dependent core checks on ARM64 as soon as device-local data access is ready.

### 2. Prove the renderer before extensive UI work (1–3 weeks)

- Add Android build configuration and APK packaging; avoid assuming `package.py --platform Android` already works.
- Cook the existing global shaders for a supported mobile Vulkan target. Resolve feature-level differences explicitly instead of requiring desktop SM6 support on all phones.
- Add synthetic scenes with generated textures: palette lookup, transparent sprites, all blend modes, depth/stencil occlusion, render-target copies and movie-plane conversion. They need no disc assets.
- Start without MSAA/supersampling/upscaling, then restore features one at a time where supported.
- Compare output against the desktop synthetic scenes; test on both GPU families before declaring the approach portable.
- Once temporary developer-only device-local disc staging is available, render a real field, battle, world-map scene and movie from the supplied discs. Use these early to catch problems absent from synthetic tests; keep all reference captures in ignored `captures/`.
- Measure memory, render-target allocations and CPU/GPU frame time.

Exit: stable Android startup, correct synthetic rendering and initial real-content comparison on a phone. This is the first go/no-go gate; comprehensive gameplay coverage follows.

### 3. Import discs and persist user data (about 1 week)

- Add an Android system file picker via Storage Access Framework.
- Initial approach: import chosen files into app-specific storage, then keep existing native filesystem readers. A content URI is not a normal filename. Check free space, show progress/cancel, and copy atomically.
- Prioritize the supplied BIN/CUE format: import each CUE plus all referenced tracks, preserving relative paths and supporting filenames with spaces. A folder picker is useful for selecting both pairs together. Validate every referenced track before accepting an import; reject references escaping the selected set. Test both discs and incomplete pairs. Keep existing CHD support as a subsequent import option.
- Initial local APK: bundle both discs through the explicit local-only packager flag. Stream first-launch copies from the APK into private physical paths used by the existing native disc reader, with progress, integrity checks, interrupted-copy recovery and sufficient-space handling. Keep Disc 1 alongside Disc 2 because upstream 0.8.1 requires it even when starting Disc 2. A system picker remains a later data-free build feature.
- Put saves/settings in explicit Android app directories. Add save import/export through the system picker so users can back them up independently of app removal.
- Apply the same location policy to optional Assets/Story files. Don't leave their path discovery tied to the executable directory.

Exit: both supplied discs import and reopen after app restart, movies retain sound, and synthetic malformed-input, cancellation and insufficient-storage tests pass. Prove save import/export and app-update retention on the phone.

### 4. Make it usable as a phone game (1–2 weeks)

- Add a multi-touch virtual D-pad, four face buttons, shoulders, Start/Select and a pause/menu control. Route these through existing pad states.
- Implement the original Slate overlay and sensor-driven dual-landscape behavior specified above, including opacity controls, editable edge placement and optional reserved side areas.
- Support simultaneous movement/buttons, safe areas, adjustable size/opacity, and optional camera drag/pinch. Keep text readable on a small screen.
- Retain Bluetooth/USB controllers and hot-plugging. Test Android Back behavior and avoid accidental app exits.
- Pause simulation and audio on backgrounding; clear held inputs on focus loss. Resume without accumulating elapsed simulation time.
- Test audio interruptions, screen lock, route changes, surface recreation and process death. Persist settings/saves atomically; do not assume the native game supports arbitrary emulator-style save states.

Exit: reliable controls and lifecycle behavior during real field navigation, dialogue, menus and battles; verify both landscape directions, simultaneous touches, transparency settings, safe areas and the missing-disc screen.

### 5. Complete both-disc gameplay validation (2–4+ weeks)

- Expand the milestone-1 PC reference using the two supplied USA BIN/CUE pairs. Compare the same revision, settings and saves on PC and Android.
- Verify movie audio using raw-sector BIN/CUE; do not convert to ISO/IMG, which upstream says loses movie audio.
- Compare title/new game, field navigation/dialogue/camera, character battle, Gear battle, world map, menus, saves, movies with sound, Battling arena, and disc switching.
- Run existing disc-dependent tests and story sweeps where practical; log every skipped case.
- Import existing saves to reach later scenarios rapidly. Compare Android regressions with upstream behavior before changing gameplay.
- Complete long sessions and both-disc coverage, including suspend/resume during gameplay and save writes.

Exit: documented local gameplay matrix covering both discs, no save corruption, and no unexplained platform-specific differences. Include an extended playthrough in addition to automated sweeps and sampled saves; upstream's completeness claims alone are not Android validation.

### 6. Optimize and distribute a beta (1–2 weeks)

- Tune custom-pass resolution, uploads, draw batching, render-target reuse and upscaling based on profiling.
- Define low/default/high device settings with measured thermal behavior. Test a midrange device rather than only a flagship.
- Validate 4 KB and 16 KB page-size compatibility for every native library with the chosen engine/toolchain.
- Produce an installable signed APK for testing and verify upgrade/save retention. If Play distribution is desired later, add AAB packaging and recheck current target-API/store requirements at that time.
- Keep remote/public outputs limited to code/runtime, with project and third-party notices. The user-requested APK containing both local discs is installed by adb and retained only in ignored local output folders.

## Effort and fallback

My planning estimate for one experienced Unreal/C++ developer is roughly 6–12 full-time weeks for a usable beta, assuming renderer changes are localized. The milestones overlap; ranges are estimates, not a delivery commitment. A beginner or part-time effort could take several months. Broad device support or a renderer rewrite can exceed this substantially.

Keep Unreal for the first feasibility spike because it preserves rendering, audio, input and UI integration. If custom rendering cannot be made reliable on the intended mobile devices, reuse WeltallCore behind a smaller Android frontend (for example SDL plus a mobile renderer). That is architecturally possible but adds a new renderer/UI/audio integration project; it is not an immediate shortcut.

## Current readiness and next action

Repository protection, the touch-input model, Android storage/lifecycle integration and local bundled-disc packaging are implemented. Windows and the Moto each passed the 1,358-case core baseline with six expected optional-undub skips, using both supplied discs; the phone also passed 731 isolated synthetic ARM64 tests. Both-disc APKs build, pass package inspection and install/update on the Moto. Matching cooker/runtime renderer settings and device-profile selection resolve startup. Both discs extract, verify and reopen on restart; the title and opening new-game movie render, and the user confirmed audible movie sound. All 57 shared rendering cases pass on Windows and Android, with a symlink-specific assertion explicitly unavailable on the phone. The later 134-case menu regression and 11-case/415-assertion touch regression pass on both platforms. Actual Android system-picker import/export preserves reference bytes; cancellation, malformed/oversized rejection and explicit overwrite confirmation work. A populated save and settings survive signed APK reinstallation byte for byte. Complete controls/lifecycle checks, gameplay save/load and the both-disc gameplay matrix remain pending. Follow `Docs/AndroidGameplayValidation.md` for milestone acceptance; alpha readiness is not established. Disc bytes and game-derived captures stay in ignored local folders.

## Sources

- Reviewed source: https://github.com/Roman215/Weltall/tree/7e81074bcdb385eb0300450a5023a7f2a21058ec
- Release/status: https://github.com/Roman215/Weltall/releases
- Epic Android setup: https://dev.epicgames.com/documentation/en-us/unreal-engine/android-quick-start
- Android Unreal guidance: https://developer.android.com/games/engines/unreal/unreal-on-android
- Android document access: https://developer.android.com/training/data-storage/shared/documents-files
- Epic Slate virtual joystick API (touch and active/inactive opacity): https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Slate/SVirtualJoystick
