# Weltall Android port: source review and implementation plan

Reviewed 17 October 2026, upstream commit `7e81074bcdb385eb0300450a5023a7f2a21058ec` (0.8.1).
Fork: https://github.com/nate-santos/Weltall

Continuation October21: upstream0.8.3 is merged. Development-only scripted PC/Moto comparisons now share normal input, use isolated save copies and collect captures/transition summaries automatically. A70-second native replay passes without warnings, matches the PC's sampled transitions exactly and retains all52 normal save/settings files. The shared PC replay remains byte-identical to its preceding reference. A longer native battle comparison and the PC original-disc-handoff confirmation are in progress; no M4/M5 or alpha-completion claim follows from the tool check. See the current AndroidHandoff.md and AndroidGameplayValidation.md before resuming. Only the Moto is available; second GPU-family coverage remains untested.

Continuation October 19: upstream `687b2ffc1ff59150177ba59df1968ee1f6074f75` (0.8.2) is merged into `android-port`, and the updated both-disc APK is installed on the Moto. The original source-review details below describe 0.8.1; current validation and remaining work are recorded at the end and in AndroidHandoff.md.

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

Hardware availability update, 21 October 2026: the user confirms only the Moto G Stylus 2025 is available. Continue gameplay and lifecycle validation on its Adreno 710; the second GPU-family check remains untested. Do not infer Mali/Immortalis compatibility from the Moto results.

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

- The user explicitly accepts the current APK-bundled plus extracted disc copies for alpha. Defer deduplication/compressed-disc or direct-package access changes until beta optimization. The measured roughly 1.41 GB of user data is principally the two extracted BIN/CUE pairs; changing Development to Shipping does not remove that working copy.
- Tune custom-pass resolution, uploads, draw batching, render-target reuse and upscaling based on profiling.
- Define low/default/high device settings with measured thermal behavior. Test a midrange device rather than only a flagship.
- Validate 4 KB and 16 KB page-size compatibility for every native library with the chosen engine/toolchain.
- Produce an installable signed APK for testing and verify upgrade/save retention. If Play distribution is desired later, add AAB packaging and recheck current target-API/store requirements at that time.
- Keep remote/public outputs limited to code/runtime, with project and third-party notices. The user-requested APK containing both local discs is installed by adb and retained only in ignored local output folders.

## Effort and fallback

My planning estimate for one experienced Unreal/C++ developer is roughly 6–12 full-time weeks for a usable beta, assuming renderer changes are localized. The milestones overlap; ranges are estimates, not a delivery commitment. A beginner or part-time effort could take several months. Broad device support or a renderer rewrite can exceed this substantially.

Keep Unreal for the first feasibility spike because it preserves rendering, audio, input and UI integration. If custom rendering cannot be made reliable on the intended mobile devices, reuse WeltallCore behind a smaller Android frontend (for example SDL plus a mobile renderer). That is architecturally possible but adds a new renderer/UI/audio integration project; it is not an immediate shortcut.

## Current readiness and next action

Current installed runtime/editor `57a5096` has verified both-disc movie audio, volume-callback correction and six actual OS audio-focus/input cases. Test/tool-only source `4a57f5c` adds two matching Windows/Moto save-failure cases and16 isolated Android save-writer interruption/retry cases, all passing. An actual native save-point pre-commit process-death case also passes: normal restart ignores the uncommitted temporary, normal retry creates a new save and removes the temporary, and cold Load restores matching party/field on Moto and PC. All49 original saves remain exact; the new Card8No02 brings protected retention to50, LastSaved107. Current5898-byte preferences remain intact. A separate real ActivityManager background kill retains the game's task and supports cold return/reload; the earlier surface-loss case passes at its recorded source. Forced Activity.recreate still exits Unreal and needs reopening. Other actual write/lifecycle boundaries, denied/delayed focus and the unfinished M5 gameplay/session scope remain open; alpha readiness is unproven. See AndroidHandoff.md for exact evidence.

At the preceding input/route checkpoint, runtime/editor `f8bf63c` extends audio interruption validation to six passing real OS cases, including actual touch/controller handlers and held/queued input cleanup. A physical Bluetooth-headphones→speaker→headphones round trip retains normal music and responsive gameplay by user confirmation, with the same game processes/audio player. All49 saves and settings remain intact. This advances M4; recreation, denied/delayed grants, save-write interruptions and the unfinished M5 gameplay/session scope below remain open.

At the preceding audio-correction checkpoint, runtime/editor was `a928879`, with a rebuilt, inspected both-disc APK installed by signed update. Actual granted OS duck/transient/permanent audio-focus cases pass all three integration tests after reproducing the old failure. This establishes simulation/audio hold and native recovery for those cases; actual touch/controller resume, denied/delayed grants, route changes and Activity recreation remain pending. All49 saves and current settings survive unchanged. Representative Disc2 flight and one destination now pass; broader world commands and sustained progression remain open. See AndroidHandoff.md for current build/evidence. The following counts remain tied to their recorded earlier revisions.

Repository protection, transparent/editable touch controls, Android storage/lifecycle integration and local bundled-disc packaging are implemented. Upstream0.8.2 is merged; the earlier storage-checkpoint runtime/editor was5ad5235. Complete Windows/Moto core suites on their recorded0275208-era source each pass1,468 cases, skip the same15 unavailable optional-mod/undub cases and pass8,541,954 assertions. Recorded isolated synthetic suites pass807 cases without skips; Moto touch passes12 cases/426 assertions. All64 shared GPU checks pass after the CRT integer-modulo correction at their recorded revisions. The current both-disc APK passes signature/same-signer, fresh actual embedded-pak integrity/both-disc listing and native alignment checks; the signed update preserves all48 saves/current settings byte-for-byte. The new actual-worker bundled-disc fault group passes all7 cases on both Windows and Moto with generated data, covering malformed input, cancellation/interruption/recovery, low/unknown space, corrupt/missing sources and completed-file reuse. Normal startup reuses existing disc copies and native Disc2 Load restores the expected state. These are new storage checks, not a relabelled broad core/GPU rerun.

Normal later Disc1/Disc2 loads, representative character/Gear victories, native legal saves and PC/Android cold loads have evidence at their recorded revisions. The updated runtime additionally passes both-disc save loading, dialogue/choices, both forced landscape surfaces and short background/resume/touch checks. Resumed Stadia tests pass basic buttons/sticks/camera and mixed hardware/touch input, and expose lingering held input after power-off/background. Fix `f382596` is now pushed and installed from its inspected both-disc APK; the physical held-disconnect/HOT-resume, touch fallback and first-input-after-reconnect retest passes on the Moto, preserving all48 saves/current settings. The README and build guide now describe the experimental port and explicit disc-directory build steps. Broader controller gameplay, original story disc handoff, regular competitive Battling, broader menus/commands, sustained device profiling, longer lifecycle/audio interruptions and extended both-disc progression remain required. Disc-storage optimization remains deferred until beta. Follow `Docs/AndroidGameplayValidation.md` for milestone acceptance; alpha readiness is not established. Disc bytes and game-derived captures stay in ignored local folders.

Previous continuation: runtime/APK5ad5235; editor-only tracedb80b0d. A later unmodified checkpoint adds one save in empty Card1/No11 (49 total, all retained). Normal Disc2 departure, flight controls/release, bridge return and Deus destination entry have PC/Moto evidence; Moto Wait/relaunch and audible world-map music also pass. The world trace leaves81 reference frames byte-identical. No broad suite rerun is claimed for that editor-only change.

Latest continuation: runtime/editor57a5096 now passes full representative raw movies on both discs, with native timing and explicit user confirmation of clear sound. A real volume change exposed a UE JNI callback clock ensure; the documented engine-setup helper corrects it, and both clips pass deliberate volume-button cycles on the rebuilt installed APK. All six actual OS audio-focus/input-recovery tests pass, physical Bluetooth route changes and real surface-loss recovery have evidence, and all49 saves/current preferences remain intact. Controlled Activity.recreate still exits Unreal and needs manual reopening, so automatic recovery remains an open limitation. Ordinary story movie transitions, original disc handoff, wider gameplay/competitive Battling and sustained progression/profiling remain required. The current gameplay matrix and handoff govern acceptance; alpha readiness is not yet established.

## Sources

- Reviewed source: https://github.com/Roman215/Weltall/tree/7e81074bcdb385eb0300450a5023a7f2a21058ec
- Release/status: https://github.com/Roman215/Weltall/releases
- Epic Android setup: https://dev.epicgames.com/documentation/en-us/unreal-engine/android-quick-start
- Android Unreal guidance: https://developer.android.com/games/engines/unreal/unreal-on-android
- Android document access: https://developer.android.com/training/data-storage/shared/documents-files
- Epic Slate virtual joystick API (touch and active/inactive opacity): https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Slate/SVirtualJoystick
