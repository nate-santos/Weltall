# Building the Android port

The Android implementation is in development. The source currently has ARM64/mobile Vulkan settings, sensor-driven rotation between both landscape sides, app-private paths and local disc bundling. An APK build and real-device validation are required before these are considered working Android features.

## Prerequisites

Use Unreal Engine 5.8 and the Android SDK, NDK and JDK versions selected by that engine's Turnkey/setup tools. Set `UE_ROOT` in the environment or the ignored `local.env` file. Existing desktop packaging remains in `Scripts/package.py`; Android packaging is separate.

The packager also accepts `JAVA_HOME`, `ANDROID_HOME`, `ANDROID_SDK_ROOT`, `NDKROOT`, `NDK_ROOT` and `ANDROID_NDK_HOME` from `local.env`. Existing environment values take precedence. These tool locations are passed only to the build process; the script does not change global environment settings or inherit arbitrary keys from the local file.

```text
python Scripts/android_package.py --check-toolchain
```

The check reports whether UE 5.8's automation launcher exists. The subsequent Unreal build performs the Android SDK checks; a successful prerequisite check alone does not prove SDK readiness.

## Local package with both discs

Commit the desired source changes first: this script exports a committed revision, defaulting to `HEAD`. It never copies an entire working directory into a build. The source export and all generated files remain under ignored `out/android/`.

```text
python Scripts/android_package.py --include-discs ../bin --print-plan
python Scripts/android_package.py --include-discs ../bin --prepare-only
python Scripts/android_package.py --include-discs ../bin --configuration Development
```

The first command validates and lists the intended locations without writing files. The second prepares an exported project without invoking Unreal. The third builds, cooks and packages the exported project. Select `--revision <commit-or-branch>` to build another committed revision.

`--include-discs` accepts a folder containing exactly two CUE sheets and their referenced BIN tracks. It rejects missing files, input references outside that folder, non-raw sector sizes and filename collisions. It supports spaces and nested folders. CUE syntax and path separators are normalized for Android in the local export; BIN data is copied unchanged. These checks establish file relationships and transfer integrity, not proof that the images are a supported game release. Use the existing core disc-reader tests for release identification and integrity validation.

The exporter includes only the two descriptors and BIN files those descriptors reference. It streams the files into `out/android/project/Content/BundledDiscs/`, writes a transfer-integrity manifest and adds the Unreal packaging setting only to that local export. `bPackageDataInsideApk=True` puts the cooked game data inside the APK. This produces a local APK intended to contain both supplied discs; verify the resulting package and both-disc discovery on the phone before calling it complete. Do not upload this APK or its staging files to Git or CI artifacts.

Without `--include-discs`, the refreshed `Content` inputs contain no disc files or bundled-disc manifest. Older bundled packages may remain in ignored build caches; Unreal recreates the staging directory for the new package. Verify package contents before sharing a data-free build. A future system file importer is still required for a usable public build. The existing `-Games=` runtime override remains available for development with device-local files.

| Location | Contents |
|---|---|
| `out/android/project/` | Exported committed project, optional local disc staging, Unreal intermediates |
| `out/android/logs/<configuration>-<build-id>.log` | Unreal packaging output |
| `out/android/packages/<configuration>/<build-id>/` | Local APK and installation output for one build |

Each build uses a fresh archive directory, so an old APK cannot be mistaken for the output of a successful command that produced no APK.

Android uses legacy pak staging, with IoStore and ZenStore disabled. The packager also passes `-SkipZenStore` to the host cooker, which otherwise reads the host's UE 5.8 packaging defaults and can produce a Zen manifest incompatible with the Android pak stage. This does not disable Unreal's derived-data cache or discard compiled engine objects.

The project refresh preserves `Binaries`, `Intermediate`, `Saved` and `DerivedDataCache`. Unchanged committed files keep their modification times, allowing Unreal to reuse compiled engine/game objects. Deleted source and old bundled-disc staging are removed; discs are staged again only when `--include-discs` is supplied. Unreal still runs cooking, staging and packaging normally. Use `--clean-export` when a completely fresh project and engine rebuild is needed. Do not refresh an export while another build of that export is running.

## First launch

The native game core opens physical files with standard C++ filesystem APIs. Files inside Unreal's APK/pak filesystem cannot be passed to it as ordinary filenames. On Android, a background worker therefore streams bundled files into `Context.getFilesDir()/Weltall/BundledDiscs/<manifest-version>/Games/` in 1 MiB chunks. The startup notice shows percentage complete. Each copied file is checked against its length and SHA-1 transfer hash and moved into place atomically; readiness is published only after all files finish. SHA-1 here checks a local copy, not authenticity or a trusted download.

Game discovery waits for successful preparation. Interrupted `.partial` files are never accepted as discs; the next launch removes abandoned partials, reuses individually verified completed files and checks free space for the remaining files before retrying. A completed version is reused after checking its readiness marker and file sizes. Updating the APK with the same disc manifest reuses the extracted copy. A changed manifest uses a separate version directory; cleanup of obsolete complete versions is not implemented yet.

Android backup rules include only the app's `Weltall/Settings` and `Weltall/Saves` folders, subject to the phone's backup preferences. Bundled discs, imported game files, exported assets and logs are excluded from cloud backup and device transfer. Unreal's Android plugin receipt stages the XML resources and adds their manifest references. The generated APK manifest/resource wiring and an actual restore still need device validation. The formats follow [Android's backup rules](https://developer.android.com/identity/data/autobackup#include-exclude-files).

Both the APK and uncompressed disc copies occupy phone storage. The first preparation checks for the total manifest size plus 256 MiB of available space, and ordinary write failures are reported on screen. Allow additional space for installation, caches, saves and any earlier bundled versions. Settings and saves use stable app-private directories independent of disc version; user asset exports also use a stable `Weltall/Assets` directory. Android removes app-private data when the application is uninstalled. Save export/import is a remaining implementation task.

The port's authored `Content/Story` documents are packaged as UFS files on Android and read through Unreal's file APIs directly into the existing story parser. They do not require an additional private extraction copy.

## Checks without Unreal or game data

```text
python -m unittest discover -s Tests/Python -p test_android_package.py -v
```

These synthetic tests cover pair selection, nested paths and spaces, missing inputs, traversal rejection, raw-sector file sizes, descriptor normalization, staged hashes, storage-space checks and the Unreal build command. Test files are generated only in ignored `out/python-tests/`.

## Standalone ARM64 core tests on a phone

The C++ core can be tested on Android before Unreal is available. Configure the existing CMake project with the engine-matched Android NDK and these target settings:

```text
-DCMAKE_TOOLCHAIN_FILE=<NDK>/build/cmake/android.toolchain.cmake
-DANDROID_ABI=arm64-v8a
-DANDROID_PLATFORM=android-31
-DANDROID_STL=c++_static
-DWELTALL_GAMES_DIR=/data/local/tmp/weltall-tests/Games
-DWELTALL_STORY_DIR=/data/local/tmp/weltall-tests/Story
```

Build the `WeltallCoreTests` target. Cross-compiled Catch2 discovery is skipped on the development computer; execute the binary through `Scripts/android_test.py` instead. The runner defaults to `out/tests/android-arm64/WeltallCoreTests` and the locally installed `out/tools/platform-tools/adb` executable. Select another binary with `--binary` and another adb with `--adb`. The phone needs USB debugging enabled and authorization for this computer; when more than one authorized device is connected, select its serial with `--serial`.

The core retains Unreal's exception-free build. The Catch2 harness uses exceptions so missing optional disc editions are reported as skipped tests instead of aborting the process. A synthetic run also sets `WELTALL_TEST_NO_DISCS=1`, preventing cached phone discs from affecting that run.

```text
python Scripts/android_test.py --preset touch
python Scripts/android_test.py --preset synthetic
python Scripts/android_test.py --stage-only --with-discs ../bin --story Content/Story
python Scripts/android_test.py --preset disc-smoke --with-discs ../bin
python Scripts/android_test.py --preset media-smoke --with-discs ../bin
```

The default `synthetic` preset excludes `[Content]` tests and stages no game inputs. `touch` exercises authored input and touch-settings logic. `disc-smoke` requires successful identification and boot-file checks for each USA disc; `media-smoke` additionally exercises movie indices, decoding and a movie stream with sound. `--filter` accepts a Catch2 expression instead of a preset. The broader `content` preset runs all content-dependent tests; it is slower, needs the story documents for story tests and can skip optional undub tests when only the USA discs are provided. Skipped cases fail the runner unless explicitly accepted with `--allow-skips`.

`--with-discs` explicitly stages the selected CUE/BIN pairs into `/data/local/tmp/weltall-tests/Games`. Each transfer uses a temporary filename, SHA-256 verification and an atomic rename. Matching remote files are verified and reused on later runs. Additional disc images in that test directory cause an error instead of silently selecting an older duplicate. `--story` stages only authored `.txt` documents under the configured Story directory. The runner does not install an APK or modify app-private saves, settings or disc copies.

Every execution gets a separate remote working directory and `TMPDIR`. JUnit, console output, transport logs and a JSON summary are pulled into ignored `out/testing/android/<run-id>/`; no screenshots are taken or uploaded. A timeout stops only the recorded process when its executable still matches the managed test binary. Local-only game-dependent reports are never CI artifacts. CI runs only the synthetic Python packaging/runner tests:

```text
python -m unittest discover -s Tests/Python -p "test_android*.py" -v
```

## Required device checks

Install a Development APK with both discs. Confirm that first launch remains responsive, progress completes, both discs are identified, the opening movie has sound and game startup works. Restart the app and confirm the copy is reused. Interrupt preparation, restart it, and test insufficient space. Install an APK update with the same signing key and confirm that settings and saves survive. Then validate shader cooking/rendering, both landscape directions, touch controls and sustained gameplay on a physical ARM64 phone. No automated packaging test replaces these checks.

## References

- [Epic Android settings](https://dev.epicgames.com/documentation/unreal-engine/android-settings-in-the-unreal-engine-project-settings)
- [Epic packaging settings](https://dev.epicgames.com/documentation/unreal-engine/API/Developer/DeveloperToolSettings/UProjectPackagingSettings)
- [Android app-specific storage](https://developer.android.com/training/data-storage/app-specific)
