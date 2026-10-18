# Android port validation

Updated October 18, 2026. Target device: Moto G Stylus 2025, Android 16, ARM64. Source branch: `android-port`. Reports, binaries, disc copies and captures are local files under ignored `out/`; this document records counts and coverage only.

## Completed checks

| Check | Result | What it establishes |
| --- | --- | --- |
| Windows Release core baseline | 1,358 passed, 0 failed, 6 skipped; 452.54 seconds | Native regression coverage with both supplied USA discs and authored story documents. The six skips require optional undub editions. |
| ARM64 synthetic core suite on Moto | 731 cases, 269,420 assertions, 0 failed/errors/skipped; 12.16 seconds | NDK-built core runs correctly with disc discovery explicitly disabled, even though earlier test discs remain cached. |
| ARM64 touch suite on Moto | 10 cases, 312 assertions, 0 failed/skipped | Multi-finger pad logic, layout/reset behavior, settings and Back-event retention. |
| ARM64 disc smoke checks on Moto | 2 cases, 0 failed/skipped | Both USA discs are identified and their boot structures are read. |
| ARM64 media smoke checks on Moto | 7 cases, 0 failed/skipped | Disc movie indices, decoder digests and streaming frame/audio data. |
| Packaging and device-runner unit checks | 28 passed | Synthetic input/path validation, staging, reports, child build environment and incremental cache preservation. |
| Repository guard checks | 16 passed | Staged/committed/outgoing-history protection against disc data and generated artifacts, including recognizable renamed content. |
| Unreal-facing game module | Compiles for Android ARM64 and Windows editor | Touch Slate widget, JNI/storage/lifecycle integration and existing custom rendering compile against UE 5.8.4's generated build configuration. This is a module compile check, not a completed engine link or APK. |

An earlier broad ARM64 run also passed 807 cases and 682,258 assertions. It included content cases whose tags were subsequently corrected, so it is not counted as the isolated synthetic baseline above.

## Build state

The development machine has Unreal Engine 5.8.4 source, Visual Studio 2022 Build Tools with the .NET Framework 4.8 SDK, Android SDK/build tools, JDK 21 and NDK r27c. The full native core/test harness builds with MSVC and Android Clang with warnings treated as errors. The core keeps exceptions disabled; the Catch2 harness enables them to report optional-disc skips correctly.

The first Unreal source build is compiling the Windows editor/cook tools and Android runtime. Its local exported project contains both BIN/CUE pairs, staged through the explicit `--include-discs` path. No APK has passed packaging or installation yet. Native decoder tests do not establish screen rendering or speaker output.

## Next device gates

1. Finish Unreal compilation and mobile Vulkan shader cooking; verify the local APK contains both bundled discs.
2. Install over USB, verify first-launch extraction/progress and restart recovery, then reach title/new game.
3. Check actual picture/movie/audio output and transparent touch composition, simultaneous controls, layout persistence and both landscape sides.
4. Check Android Back, controller unplugging, suspend/resume, saves and representative field/battle/world-map/disc-switch scenarios.
5. Measure sustained frame time, memory, battery/thermal behavior and validate upgrade/save retention before calling the port ready for regular play.

The complete rebuilt ARM64 suite with both discs is currently running. Results will replace this pending status after the reports are complete.
