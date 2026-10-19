# Android gameplay validation matrix

This is the acceptance record for milestones 1–5 of [AndroidPortPlan.md](AndroidPortPlan.md). Alpha readiness remains unproven until the relevant rows have direct runtime evidence. A core test pass establishes game logic coverage; it does not establish rendered output, audible sound or usable phone controls.

Target: Moto G Stylus 2025, Android 16, Adreno 710, both supplied USA BIN/CUE pairs. Compare PC and Android using the same source revision, settings and save inputs. Keep saves, logs, captures, extracted data and detailed story-sweep output under ignored `out/`; this tracked document contains authored procedure and outcome summaries only.

## Current evidence

| Gate | Observed result | Evidence / remaining work |
| --- | --- | --- |
| Disc and core baseline | Both USA discs identified; Windows and Moto each passed 1,358 cases with the same six optional-undub skips | Counts and local report paths in [AndroidValidation.md](AndroidValidation.md). These are not gameplay passes. |
| Isolated ARM64 synthetic baseline | 731 cases and 269,420 assertions passed without disc access | Existing device report, recorded in AndroidValidation.md. |
| Packaged APK inspection | Signature, SDK settings, compiled landscape orientations, backup XML, ZIP alignment, seven ARM64 ELF libraries with 16 KiB load alignment, pak listing and pak integrity passed | `out/android/validation/20261018T170835Z-1329a339/summary.json` and adjacent tool reports. Pak listing contains both BIN/CUE pairs; runtime extraction must still verify every copied file. |
| APK installation | Installed successfully through ADB; a populated save and settings survive a signed reinstall/update byte for byte | Latest installed source `56affc5`, archive `20261018T235126Z-da0113a7`, passed the same package inspection. `out/testing/android-app/save-populated-update-retention/comparison.json`; subsequent restart rediscovers both discs (`save-update-restart/logcat.txt`). |
| Vulkan selection | Passed after enabling AndroidDeviceProfileSelector | Device selects Android_Adreno7xx_Vulkan and initializes its Vulkan RHI/swapchain. `out/testing/android-app/20261018-first-launch/logcat-profile-fix.txt`. |
| First app startup | Passed with shared mobile renderer settings; title and opening new-game movie render | UE 5.8 defaults the cooker's mobile path to HDR/deferred, while the former Android-only HDR override selected forward rendering at runtime. `ca17c38` makes both agree. Normal launch uses no diagnostic command line. Local captures/logs: `out/testing/android-app/shared-mobile-running`, `title-touch-confirm`, `new-game-touch`. |
| Bundled disc preparation / restart | Both discs copied and verified on first launch; a restart discovered both without repeating the copy | `shared-mobile-startup/logcat.txt` records readiness; `restart-normal/logcat.txt` records rediscovery. Interrupted-copy and insufficient-space cases remain pending. |
| Windows synthetic GPU baseline | All 57 rendering cases passed; zero failed or unrun, including a repeat after moving tests into a shared Development module | `out/testing/unreal/windows-render-baseline/report/index.json` and `windows-render-shared/report/index.json`, D3D12 SM6 on AMD Radeon. One case produced two expected malformed-texture warnings. This is not the real-content gameplay reference. |
| Android synthetic GPU tests | All 57 cases reported success, zero failed or unrun, 9.24 seconds | `out/testing/android-app/rendering-full-complete/report/index.json`, Vulkan ES3.1 on Adreno 710. One case has the same two expected malformed-texture warnings as Windows. The partial-link check explicitly skipped its symlink-specific assertion because this Android storage location cannot create links. Driver pipeline-creation messages remain under assessment despite successful pixel checks. The APK omits the HTML template; its JSON report is complete. |
| Basic touch / screen awake | Confirm and D-pad Up navigate the title and start a new game; Android logs confirm the game's keep-screen-on flag | Direct single-touch injection through the phone's touch input path. Multi-touch, rotations, configuration and lifecycle checks remain pending. |
| Opening-movie audio | User confirmed audible sound from the Moto during the opening movie | October 18, 2026. This validates this movie/output route; other movies and audio interruptions remain pending. |
| Save-document model | Four native save/browser cases passed, 142 assertions; broader 134-case menu suite passed on Windows and ARM64 | `out/testing/save-documents-windows-tests.xml`, `menu-regression-windows.xml`, `out/testing/android/20261018T195257Z-453f0abc/summary.json`. Java/JNI build passes in `65ac5d3`. |
| Android save picker | Import preview leaves storage unchanged; confirmed import matches the reference save; exported memory card matches the whole input card byte for byte | `out/testing/android-app/save-import-confirmed/comparison.json`, `save-export-roundtrip/comparison.json`. Device cancellation, malformed-card rejection and the 1 MiB limit display readable messages; the existing save remains identical (`save-errors-complete/comparison.json`). Fixtures were generated locally from the supplied disc's initial state; they are transfer evidence, not gameplay progression. |
| Save overwrite confirmation | First confirmation on occupied Card 1 asks again and preserves its file metadata/bytes; second confirmation writes the same valid reference bytes | `out/testing/android-app/save-overwrite-card1-asked` and `save-overwrite-card1-written`, screenshots and comparison reports. Import defaults to the first free card, so the test explicitly selected occupied Card 1. This does not validate gameplay save/load continuity. |
| Control settings / update retention | Idle/pressed opacity, scale and side-area changes work and remain byte-identical after an APK update; an edited position survives restart | `out/testing/android-app/controls-update-retention/comparison.json`, `controls-moved-retention/comparison.json`. The moved Cross target works after restart. Reset restores 25% idle / 55% pressed / 100% size / sides off and zero offsets (`controls-defaults`). |
| Invisible control recovery | At 0% displayed idle/pressed visibility, D-pad and moved action targets still work; the visible Controls button reopens settings | `out/testing/android-app/controls-zero-hit`, `controls-zero-recovery`, `controls-edit-drag`, `controls-moved-hit`. Tested through the installed Slate overlay, then restored defaults. |
| Simultaneous pointers / cancellation | D-pad plus Square plus Triangle stay active; releasing Square leaves D-pad/Triangle active; cancellation clears all | Local shell-injected touchscreen sequence on the Saves page, with captures `out/testing/android-app/multitouch-final-three`, `multitouch-final-two`, `multitouch-final-cancelled`. Final helper completes with exit 0; earlier probe attempts are retained separately. Visual held-state evidence complements native input assertions; real field/battle combinations remain pending. |
| Landscape surfaces / safe areas | Both forced landscape rotations render the menu; the Controls panel opens at its shifted safe-area position and Android Back closes it | `out/testing/android-app/rotation-probe/summary.json`, `rotation-1*`, `rotation-3*`. The process remains alive and original `free` / `default` rotation settings are restored. This checks surface/inset behavior; a physical sensor-driven flip remains unverified. |
| Quick touch presses | Correction retains a completed tap until the game samples input; 11 cases / 415 assertions pass on Windows and ARM64 | `out/testing/touch-quick-tap-windows.xml`, `out/testing/android/20261019T000032Z-2c24e6b8/summary.json`. Zero-duration device touches navigate to Options and Saves (`quick-tap-device-options`, `save-page`). This does not establish simultaneous-finger behavior in Slate. |
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
