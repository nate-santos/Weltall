# Android port validation

Updated October 20, 2026. Target device: Moto G Stylus 2025, Android 16, ARM64. Source branch: `android-port`. Reports, binaries, disc copies and captures are local files under ignored `out/`; this document records counts and coverage only.

## Current save-interruption validation

The actual native save-point UI now passes a pre-commit process-death/retry check on installed runtime57a5096. A verified native breakpoint stops before save rename with a complete closed temporary, absent new destination and unchanged pointer. Force-stop leaves all49 original saves/preferences unchanged; normal restart ignores the temporary. Normal retry into the previously empty slot creates checksum-valid Card8No02, removes the temporary and updates LastSaved107. All50 saves pass retention. Cold native Load restores the expected party/field; the identical new save bytes/settings load on matching PC, with reviewed matching state/scene and differing viewport layouts. Evidence: `out/testing/android-app/actual-save-before-commit-verified-code-57a5096`, `actual-save-retry-cold-*`, `actual-save-cold-final-*`; PC `actual-save-retry-native-reference` (70 simulated/44.203 wall seconds, source unchanged, outputs present). No production code change/rebuild was needed. This is one actual pre-commit game-write case, not power-loss durability or every lifecycle/write boundary; broader M4/M5 remain open. AndroidHandoff.md records preservation and continuation details.

Source4a57f5c adds standalone tests/tools only; installed runtime/editor remain57a5096. Windows and Moto each pass two generated-data save-failure cases, 21 assertions without failure/skip. The standalone native Moto probe passes all16 process-death/suspension cases at eight production save-store libc boundaries, with independently reopened exact/checksummed bytes, valid pointers, unrelated-slot retention and successful retry. Reports: `out/testing/android/20261020T174100Z-37e3bb2b/summary.json`, `out/testing/save-interruption/20261020T173559Z-f1b1f133/summary.json`. The probe's source/build hashes and transport are beside its report. All33 Python tooling checks pass. No production save/game behavior changed; no APK rebuild or broad core/GPU rerun is claimed. These tests do not establish sudden-power-loss durability or actual save-point UI interruption.

A separate ActivityManager kill removes the background game's processes and retains its task. Returning it cold-starts successfully, with all49 saves/current preferences unchanged. AndroidHandoff.md records exact evidence and continuation. Broader M4/M5 remain open.

## Previous raw-movie and volume-callback validation

Runtime/editor `57a5096` includes the Development-only raw-movie check and the explicit UE source-setup correction for its Android volume receiver's clock. Initial full Disc1/Disc2 cases pass and separate replays receive the user's clear-sound confirmation. Disc1's listening replay exposes an engine ensure on a real volume change; symbolication resolves the JNI callback's game-thread-only clock access. The bounded setup helper corrects that receiver timestamp using Unreal's thread-safe approximate clock, with the known uncorrected source blocked by the packager.

After rebuilding/relinking the corrected Android engine object, both full movie cases pass while real volume buttons change10→11→10. Disc1 renders1062 pictures in70.765 seconds; Disc2 renders505 in33.600 seconds. Each report has one success and zero warnings/failures/unrun, and native UI-thread logs prove both volume events. The six actual OS focus/input-recovery cases also pass,19.489 seconds test/27.969 elapsed. All31 Python tooling and16 guard tests pass. No broad core/GPU rerun is inferred. Matching PC clips have complete non-silent stereo output and sampled frame review; direct movie-screen checks do not replace ordinary story transitions or the original disc handoff.

The rebuilt both-disc APK passes fresh actual embedded-PAK integrity/both-disc listing, signature/same signer, zipalign16KiB and seven native ELF alignments, then installs by signed update. All49 saves/checksums, LastSaved106 and current5898-byte settings remain identical; media volume returns to10. Evidence and exact APK/hash are in AndroidHandoff.md, with current case reports under `out/testing/android-app/raw-movie-disc{1-07,2-01}-volume-keys-57a5096` and `audio-focus-volume-clock-57a5096`. M4/M5 and alpha readiness remain incomplete.

## Bundled-disc fault validation on5ad5235

The actual bundled-disc worker's seven generated-data automation cases pass on Windows and Moto:7 success, zero warnings/failures/unrun,0.151/0.142 seconds test time respectively. They exercise parser/filename validation, native copies and verification, ready restart reuse, low/unknown space, initial/in-flight cancellation, abandoned-partial cleanup and completed-file reuse, missing/corrupt sources and sibling-file isolation. The tests are development-only and use small generated files. Final PC source hashes match committed5ad5235. The first PC NUL-fixture failure is corrected and retained as historical evidence, not counted as a pass. Reports: `out/testing/unreal/bundled-discs-faults-pc-final/report/index.json`, `out/testing/android-app/bundle-faults-moto-5ad5235/index.json`.

The new both-disc APK builds and passes fresh actual embedded-PAK integrity/both-disc listing, same-signer, zipalign16KiB and seven ELF alignment checks before signed update installation. Normal startup discovers both discs and native Card8No01 Load/Disc2 confirmation restores the expected scene/party/stats/gold and responsive menu. All48 saves/checksums and current5889-byte settings remain byte-identical. All nine existing disc/readiness/verification file metadata records remain unchanged through the tests and normal restart; generated fixtures and the launch override are removed. AndroidHandoff.md records APK/hash/log paths and current phone state. Historical complete core/GPU counts below stay tied to their earlier source revisions. Broader gameplay, movie audio and lifecycle acceptance remain pending.

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
