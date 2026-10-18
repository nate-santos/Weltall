# Android port validation

Updated October 18, 2026. Target device: Moto G Stylus 2025, Android 16, ARM64. Source branch: `android-port`. Reports, binaries, disc copies and captures are local files under ignored `out/`; this document records counts and coverage only.

## Completed checks

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
