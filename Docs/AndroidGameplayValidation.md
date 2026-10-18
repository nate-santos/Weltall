# Android gameplay validation matrix

This is the acceptance record for milestones 1–5 of [AndroidPortPlan.md](AndroidPortPlan.md). Alpha readiness remains unproven until the relevant rows have direct runtime evidence. A core test pass establishes game logic coverage; it does not establish rendered output, audible sound or usable phone controls.

Target: Moto G Stylus 2025, Android 16, Adreno 710, both supplied USA BIN/CUE pairs. Compare PC and Android using the same source revision, settings and save inputs. Keep saves, logs, captures, extracted data and detailed story-sweep output under ignored `out/`; this tracked document contains authored procedure and outcome summaries only.

## Current evidence

| Gate | Observed result | Evidence / remaining work |
| --- | --- | --- |
| Disc and core baseline | Both USA discs identified; Windows and Moto each passed 1,358 cases with the same six optional-undub skips | Counts and local report paths in [AndroidValidation.md](AndroidValidation.md). These are not gameplay passes. |
| Isolated ARM64 synthetic baseline | 731 cases and 269,420 assertions passed without disc access | Existing device report, recorded in AndroidValidation.md. |
| Packaged APK inspection | Signature, SDK settings, compiled landscape orientations, backup XML, ZIP alignment, seven ARM64 ELF libraries with 16 KiB load alignment, pak listing and pak integrity passed | `out/android/validation/20261018T170835Z-1329a339/summary.json` and adjacent tool reports. Pak listing contains both BIN/CUE pairs; runtime extraction must still verify every copied file. |
| APK installation | Installed successfully through ADB; subsequent updates preserve app data | Latest installed source `ca17c38`, archive `20261018T191746Z-4ffe3f1c`, passed the same package inspection. Save/update retention still needs a populated-save round trip. |
| Vulkan selection | Passed after enabling AndroidDeviceProfileSelector | Device selects Android_Adreno7xx_Vulkan and initializes its Vulkan RHI/swapchain. `out/testing/android-app/20261018-first-launch/logcat-profile-fix.txt`. |
| First app startup | Passed with shared mobile renderer settings; title and opening new-game movie render | UE 5.8 defaults the cooker's mobile path to HDR/deferred, while the former Android-only HDR override selected forward rendering at runtime. `ca17c38` makes both agree. Normal launch uses no diagnostic command line. Local captures/logs: `out/testing/android-app/shared-mobile-running`, `title-touch-confirm`, `new-game-touch`. |
| Bundled disc preparation / restart | Both discs copied and verified on first launch; a restart discovered both without repeating the copy | `shared-mobile-startup/logcat.txt` records readiness; `restart-normal/logcat.txt` records rediscovery. Interrupted-copy and insufficient-space cases remain pending. |
| Windows synthetic GPU baseline | All 57 rendering cases passed; zero failed or unrun, including a repeat after moving tests into a shared Development module | `out/testing/unreal/windows-render-baseline/report/index.json` and `windows-render-shared/report/index.json`, D3D12 SM6 on AMD Radeon. One case produced two expected malformed-texture warnings. This is not the real-content gameplay reference. |
| Android synthetic GPU tests | All 53 currently available cases passed, zero failed or unrun, 8.53 seconds | `out/testing/android-app/rendering-tests-complete/report/index.json`, Vulkan ES3.1 on Adreno 710. Four additional editor-only tests are being enabled for matching coverage. Driver pipeline-cache messages need further assessment despite successful pixel checks. HTML report template is absent from the APK; the JSON result is complete. |
| Basic touch / screen awake | Confirm and D-pad Up navigate the title and start a new game; Android logs confirm the game's keep-screen-on flag | Direct single-touch injection through the phone's touch input path. Multi-touch, rotations, configuration and lifecycle checks remain pending. |
| Broader GPU comparison | Pending | No Mali/Immortalis device result exists; do not claim cross-family portability. |

## Gameplay matrix

Each pending cell requires a recorded run on that platform/disc. For a save-based case, record the local save identifier/hash, entry state, matching settings, actions, outcome and evidence paths in the ignored run report. Include entry/exit behavior, control response and save continuity, rather than accepting a single static picture as gameplay validation.

| Scenario | PC Disc 1 | Android Disc 1 | PC Disc 2 | Android Disc 2 |
| --- | --- | --- | --- | --- |
| Title, new game / appropriate disc entry | Title reference rendered; new game pending | Title and opening movie entered through touch; later progression pending | Pending | Pending |
| Field walking, exits and collision | Pending | Pending | Pending | Pending |
| Dialogue, choices and scripted progression | Pending | Pending | Pending | Pending |
| Field camera and required shoulder actions | Pending | Pending | Pending | Pending |
| Character battle, commands and return to field | Pending | Pending | Pending | Pending |
| Gear battle, commands and return to field | Pending | Pending | Pending | Pending |
| World-map movement, camera and destinations | Pending | Pending | Pending | Pending |
| Menus, options and party management | Pending | Pending | Pending | Pending |
| Save, reload and matching restored state | Pending | Pending | Pending | Pending |
| Movies with audible sound from raw BIN/CUE | Pending | Pending | Pending | Pending |
| Battling arena input and completed round | Pending | Pending | Pending | Pending |
| Extended play with actual progression | Pending | Pending | Pending | Pending |

Disc-switch validation is a separate transition: reach the original Disc 1 → Disc 2 handoff, continue with both discs present, and compare restored/progressed state on PC and Android. Automated story sweeps and sampled saves supplement the extended play sessions; they do not replace them.

## Storage, controls and lifecycle gates

- Bundled extraction: responsive progress, both-disc discovery, successful hashes, restart reuse, interrupted-copy recovery and non-destructive insufficient-space testing.
- Save access: Android system-picker import/export, byte/state round trip and retention through an APK update signed with the same key. Do not uninstall or clear app data to work around a failure.
- Touch: movement plus two action fingers, diagonals, quick taps, all required buttons, adjustable transparency including invisible hit targets, scale/position persistence and visible reset/recovery.
- Layout: both landscape directions, cutout and navigation safe areas, readable dialogue/battle menus, reserved side areas and no stuck input after capture cancellation or rotation.
- Navigation: Android Back closes control settings or cancels in-game without unexpectedly exiting; missing-disc UI remains usable.
- Hardware controllers: connect/disconnect and combined hardware/touch state; lifting one input source must not release another source's held button.
- Lifecycle/audio: background/resume, screen lock, surface recreation, process death, audio interruptions/route changes and save writes during interruptions. Verify simulation timing does not jump and saves remain readable.
- Long sessions: record duration, progression, CPU/GPU frame time, memory and thermal observations; investigate crashes, stalls, corruption and unexplained PC/Android differences before alpha approval.

## Alpha exit audit

For each milestone exit condition, link authoritative local evidence and classify it as passed, failed, pending or blocked. Preserve every skipped test with its reason. An expected optional-undub skip is different from an untested supplied-disc scenario. Alpha readiness requires the completed both-disc gameplay matrix, an extended playthrough, no save corruption and no unexplained platform-specific differences. At present, no gameplay row is passed and no alpha-readiness claim is made.
