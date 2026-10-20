# Android port validation

Updated October 20, 2026. Target device: Moto G Stylus 2025, Android 16, ARM64. Source branch: `android-port`. Reports, binaries, disc copies and captures are local files under ignored `out/`; this document records counts and coverage only.

## Current gameplay and screen-lock validation

Runtime/editor f382596 now has matching PC/Moto representative menus on both native Android-created cave/Disc2 victory saves: party-panel swap/restore, status/member changes, skills, equipment and inventory lists, abilities, Gear pages, Display/Sound options and cancellation to the field. Disc2 equipment-preview cancellation, healing-item count/HP changes and Ether targeting/HP/EP changes match. These are unsaved session changes, not modifications to source saves. The three accepted PC references exit0 with source bytes unchanged; earlier mistimed Disc1 input scripts are preserved as incomplete, not passes.

The Moto's146.241-second menu screen-lock/Dozing case keeps both processes and the same selection, advances the native clock by only3 seconds across lock/resume, and accepts single-step/released touch navigation and field return. Android audio-renderer suspend/resume appears in logs; audible audio focus/routes and other lifecycle cases remain separate. All48 saves/checksums and current5889-byte settings are preserved exactly. See AndroidGameplayValidation.md for private evidence paths, measured outcomes, coverage limits and the milestone exit audit. No broad core/GPU rerun is attributed to this input-only revision, and alpha readiness is still pending.

## Upstream 0.8.2 revalidation (October 19, 2026)

Installed runtime `0275208` includes upstream `687b2ff`, Android integration and the verified CRT pixel-modulo fix. Windows and Moto each pass 807 synthetic cases / 390,303 assertions without skips. Moto touch checks pass 12 cases / 426 assertions. All 64 shared GPU checks pass on the updated PC editor and installed Moto, including CRT, screen wave, subtitle overlay and Beetle textures; one texture-negative-input test reports expected warnings.

The final both-disc APK is 1,572,463,499 bytes, SHA-256 `f7aa09880cdceed8127ae1090a26a1804bd3b6f841da5ba2a85c4bc6dfd46b32`. Signature, ZIP validation, all seven ARM64 ELF load alignments at 16 KiB, embedded-pak integrity and both BIN/CUE pairs pass. App update and subsequent graphics tests preserve all 48 saves and current preferences byte-for-byte. Normal title/load/disc-2 confirmation restores the native Disc 2 victory save to its field scene.

Complete corrected Windows and Moto core suites each finish with 1,483 cases: 1,468 passed, 15 skipped, zero failures; 8,541,954 assertions pass on each. The Moto run takes 1,415.031 seconds. Skip messages match: ten unavailable optional-undub cases and five unavailable Perfect Works 0.11.4 cases; no supplied-USA-disc case is skipped. Windows evidence is `out/testing/upstream-082-windows-clean-full.log`; Moto evidence is `out/testing/android/20261019T233258Z-3913d81c/report.catch.xml`. The runner is corrected to exclude skip markers from its assertion total; the original device summary includes those 15 markers, so use the unchanged native XML count above. All 44 Python packaging/runner/guard checks pass.

Both forced landscape surfaces pass game/Controls/Back checks with original rotation restored. Short background/resume retains both app processes and accepts subsequent D-pad/Confirm input. Native File/Load switches normally from the Disc2 victory save to Card6No12's Disc1 lobby; Hammer dialogue/choices advance without changing save data. All 48 saves retain their hashes/checksums, and final user settings are byte-identical. These are bounded smoke checks; long suspension/audio focus, original story disc handoff and wider gameplay still require acceptance. The user deferred updated Stadia stick/button testing until later.

The initial Windows full run's sole failed case was the new regression test using the digital-only consumer; its corrected Battling input check now passes the complete suites. The first PC GPU run exposed the real CRT issue despite exit0; unchanged strict assertions pass after integer modulo. The three optional Perfect Works fixture tests are classified as Content so the isolated suite uploads/requires no mod files. See AndroidHandoff.md for exact local reports and continuation details. No build/test/reference job remains active. Earlier baseline results below refer to the pre-merge runtime.

## Controller follow-up at usage handoff

Stadia physical checks on0275208 pass basic face buttons, D-pad, both sticks/camera/reset, release behavior and controller movement combined with touch jump, confirmed by the user and private input/frame captures. Shoulder/trigger/thumb-click signals arrive. Held-direction power-off backgrounds the game without killing either process, but resumed untouched menus retain a direction after device removal: cleanup fails. Proposed fixf382596 is pushed and builds/links for Windows editor and Android. Both-disc APK `20261020T003129Z-4a380075/Weltall-arm64.apk` passes signature, zipalign16KiB, seven native ELF alignments, fresh actual embedded-PAK integrity and both-disc listing. It remains uninstalled; physical retest and reconnect-first-input acceptance are pending. No prior full core/GPU pass is relabelled as a test of this new input code. All48 saves remain unchanged. Current settings add32 upstream0.8.2 defaults with all old values preserved; final retention passes against the newer5889-byte baseline. AndroidHandoff.md gives the exact APK/hash and resume steps.

## Installed controller-fix validation

Subsequent continuation installs the inspected f382596 APK successfully and preserves all48 saves/settings. Physical direction-held Stadia power-off, HOT same-process resume, untouched-menu stability, touch fallback and first-press input after reconnect all pass on the Moto. Captures `controller-fix-resumed-idle-{1,2,3,4}` retain Abilities while the clock advances; touch Down selects Gear once, then cancel restores the field. The user confirms reconnect/X/stick/B behavior; raw input traces accompany it. Final retention passes (`controller-fix-final-*`). The previous uninstalled/retest-pending state above is historical. See AndroidHandoff.md for the current state and remaining coverage.

## Earlier baseline checks

| Check | Result | What it establishes |
| --- | --- | --- |
| Windows Release core baseline | 1,358 passed, 0 failed, 6 skipped; 452.54 seconds | Native regression coverage with both supplied USA discs and authored story documents. The six skips require optional undub editions. |
| Complete ARM64 core suite on Moto | 1,358 passed, 0 failed/errors, 6 skipped; 8,273,021 assertions; 1,638.95 seconds | Both USA discs and authored story documents; the skipped cases match the optional-undub Windows skips. |
| ARM64 synthetic core suite on Moto | 731 cases, 269,420 assertions, 0 failed/errors/skipped; 12.16 seconds | NDK-built core runs correctly with disc discovery explicitly disabled, even though earlier test discs remain cached. |
| ARM64 touch suite on Moto | 10 cases, 312 assertions, 0 failed/skipped | Multi-finger pad logic, layout/reset behavior, settings and Back-event retention. |
| ARM64 disc smoke checks on Moto | 2 cases, 0 failed/skipped | Both USA discs are identified and their boot structures are read. |
| ARM64 media smoke checks on Moto | 7 cases, 0 failed/skipped | Disc movie indices, decoder digests and streaming frame/audio data. |
| Packaging and device-runner unit checks | 28 passed | Synthetic input/path validation, staging, reports, child build environment and incremental cache preservation. |
| Repository guard checks | 16 passed | Staged/committed/outgoing-history protection against disc data and generated artifacts, including recognizable renamed content. |
| Unreal-facing game module | Compiles for Android ARM64 and Windows editor | Touch Slate widget, JNI/storage/lifecycle integration and existing custom rendering compile against UE 5.8.4's generated build configuration. This is a module compile check, not a completed engine link or APK. |
| Moto graphics capabilities | Adreno 710, Vulkan 1.3.284 | Driver reports the sampled integer/float textures, color/depth attachments and buffer limits checked for this renderer. Actual draw output remains a device gate. |
| Android backup resources | Unreal plugin processing and SDK 36 resource compilation/linking pass | Both manifest references resolve to staged XML allowing only saves/settings. Finished game APK wiring and restore behavior remain unverified. |

An earlier broad ARM64 run also passed 807 cases and 682,258 assertions. It included content cases whose tags were subsequently corrected, so it is not counted as the isolated synthetic baseline above.

## Build state

The development machine has Unreal Engine 5.8.4 source, Visual Studio 2022 Build Tools with the .NET Framework 4.8 SDK, Android SDK/build tools, JDK 21 and NDK r27c. The full native core/test harness builds with MSVC and Android Clang with warnings treated as errors. The core keeps exceptions disabled; the Catch2 harness enables them to report optional-disc skips correctly.

The first full Development APK built successfully on October 18, 2026, after correcting host ZenStore cooking and enabling Unreal's Android permission helper. Both BIN/CUE pairs were staged through the explicit `--include-discs` path. The archived APK is `out/android/packages/Development/20261018T170835Z-1329a339/Weltall-arm64.apk` (1,573,857,611 bytes). Final package-content/signature/alignment checks and installation remain pending. Native decoder tests do not establish screen rendering or speaker output.

The [October 18 handoff](AndroidHandoff.md) records the successful build log, tool locations and next steps. The successful export includes the newest backup receipt/resources. Gradle could not parse the locally named NDK folder as a version and skipped debug-symbol stripping; investigate that warning before preparing a smaller package. No build remains running at this handoff.

## Next device gates

1. Verify the successful local APK contains both bundled discs, valid signatures, expected manifest/resources and compatible native/ZIP alignment.
2. Install over USB, verify first-launch extraction/progress and restart recovery, then reach title/new game.
3. Check actual picture/movie/audio output and transparent touch composition, simultaneous controls, layout persistence and both landscape sides.
4. Check Android Back, controller unplugging, suspend/resume, saves and representative field/battle/world-map/disc-switch scenarios.
5. Measure sustained frame time, memory, battery/thermal behavior and validate upgrade/save retention before calling the port ready for regular play.

The complete rebuilt ARM64 suite finished successfully. Its local reports are in `out/testing/android/20261018T134322Z-0167d074/`; the isolated synthetic report is in `out/testing/android/20261018T134229Z-d27f1aa3/`. These folders remain ignored and are not uploaded.
