# Android port handoff — October 18, 2026

The user requested a clean handoff while the current machine build finishes. Leave that build running. Resume development after checking its actual result; do not start a second build against the same exported project.

## Project and authorized scope

- Fork: <https://github.com/nate-santos/Weltall>, branch `android-port`, upstream `Roman215/Weltall`.
- Checkout: `C:/Users/baldr/Documents/code/xenogears/Weltall`.
- Last implementation commit at handoff: `67654f440835edf4c3c795a6c360b17479059f6f` (Android backup rules). The handoff itself is a later documentation commit.
- Continue pushing reviewed source changes to this branch. The user authorized Unreal's source build, the Epic/GitHub link, tool installation, Android SDK license acceptance and USB installation/testing.
- Test phone: **Moto G Stylus 2025**, Android 16, ARM64, Adreno 710. This replaces the earlier Pixel 7a target. USB debugging was authorized; check its current connection before device work.
- Intended deliverable: a local APK containing both supplied USA discs, with rotation between both landscape sides and transparent, adjustable controls that obscure as little of the picture as possible.
- Both BIN/CUE pairs are in `C:/Users/baldr/Documents/code/xenogears/bin`, outside the checkout. Keep all discs, extracted game content, packages, captures and generated test/build output local and ignored. Never upload the bundled APK or game-dependent reports to GitHub/CI.
- Existing `.gitignore`, `.githooks` and `Scripts/repo_guard.py` protect staged source and outgoing history. Stage explicit source/documentation paths, inspect their diffs and run the guard before committing. These protections do not replace reviewing what is being published.

## Running build: inspect before doing anything else

Snapshot: **2026-10-18 15:01 UTC**. The first full Unreal build was still running, beyond action **1,764 of 4,395**, with no compiler error observed. The action count is not an overall progress percentage or ETA. It is compiling both Windows editor/cooking tools and the Android runtime; low parallelism on this machine makes the initial engine build slow.

| Item | Location/state |
| --- | --- |
| Unified exec session | `13121`; still active at handoff preparation. The session handle might not survive a new agent, so also inspect the log and build processes. |
| Current log | `out/android/logs/Development-20261018T133325Z-resume.log` |
| Live exported project | `out/android/project` |
| Current archive destination | `out/android/packages/Development/20261018T133325Z-resume/` |
| Engine | `C:/Users/baldr/ue58`, UE 5.8.4, branch `5.8`, commit `4dced763583b8fa05d214b133c2c21b73da300d4` |
| Environment | Already configured in ignored `local.env`; do not print or commit the file. |

Read the current log from the checkout:

```powershell
Get-Content -LiteralPath out/android/logs/Development-20261018T133325Z-resume.log -Tail 60
```

If the tool session is available, poll it without sending input. If it is unavailable, inspect the relevant Unreal/AutomationTool/compiler processes as well as the log. A quiet log alone does not establish that the build stopped. Do not terminate the build, refresh/delete its project, run `--prepare-only`, or start another UAT invocation while it is active. No game APK existed in the current archive at this snapshot.

### Important: the live export predates the newest source

The active export started from `f5473f6`. Later touch/input/storage C++ compile fixes were copied into it and individually compiled against UE's generated Android and Windows response files. **The `Weltall.Build.cs` Android receipt and backup UPL/XML added in `67654f4` are not in the live export.** They were deliberately left alone while the build was running.

After the current build exits, inspect success/failure and retain its log. Fix any routine build failure in canonical source and commit the fix. Then run the normal packager against the latest committed source, even if this first build produced an APK:

```powershell
Set-Location 'C:/Users/baldr/Documents/code/xenogears/Weltall'
& 'C:/Users/baldr/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' Scripts/android_package.py --include-discs ../bin --configuration Development
```

Use elevated tool execution when required for writes to the engine outside the workspace. The packager exports committed `HEAD`, so commit intended source changes first. Its default refresh now preserves `Binaries`, `Intermediate`, `Saved` and `DerivedDataCache`, retains timestamps for unchanged source and creates a fresh archive directory. **Do not use `--clean-export`** during normal continuation: it discards compiled caches. Do not repeat the large engine download or tool installation; those setup steps completed.

The current session was launched directly through the packager's `run_build` helper to resume its existing export. Independently verify the real APK output; a helper process ending alone is not proof of a packaged game.

## Completed implementation and checks

The source includes Android ARM64/Vulkan configuration, sensor landscape, app-private storage, background extraction of both bundled discs with progress/hash/size checks and interrupted-copy recovery, lifecycle pause/resume handling, and original procedural Slate controls. The controls combine hardware and touch input, support multiple fingers, adjustable transparency/size, saved movable positions and optional reserved side areas. Fresh Android settings use the original button symbols. Android Back handling and controller unplugging have source fixes; their complete app behavior still needs device checks.

The newest backup plugin resources include only player settings and saves in backup/transfer rules. The Unreal plugin processor and Android resource compiler successfully processed them, but the final game APK and an actual restore have not been tested.

| Completed check | Result / local evidence |
| --- | --- |
| Windows complete native core | 1,358 passed, 0 failed, 6 optional-undub skips, 452.54 seconds; `out/tests/windows-android-baseline/ctest.log` |
| Moto complete ARM64 core | 1,358 passed, 0 failed/errors, the same 6 optional-undub skips, 8,273,021 assertions, 1,638.95 seconds; `out/testing/android/20261018T134322Z-0167d074/summary.json` |
| Moto isolated synthetic core | 731 cases, 269,420 assertions, 0 failures/errors/skips, 12.16 seconds, disc discovery disabled; `out/testing/android/20261018T134229Z-d27f1aa3/summary.json` |
| Moto touch / disc / media subsets | Touch: 10 cases, 312 assertions; disc smoke: 2 cases; media smoke: 7 cases. All passed. |
| Python package/device runner | 28 passed, including incremental export/cache behavior |
| Repository guard | 16 passed; [remote checks for `67654f4`](https://github.com/nate-santos/Weltall/actions/runs/39136035083) also passed |
| UE-facing game module | Individually compiled for Android ARM64 and Windows editor; `out/android/compile-check/` |
| Backup resource integration | Actual UE plugin processing and SDK 36 resource compile/link passed; `out/tools/backup-check/` |
| Device graphics capabilities | Required checked texture/attachment formats and buffers reported supported; `out/tools/logs/moto-vulkan.json` |

The six skips require optional undub disc editions, not either supplied USA disc. An older 807-case run included content cases whose tags were subsequently corrected; use the 731-case run as the isolated synthetic baseline. Native decoding, graphics capabilities and module compilation do **not** establish that the Unreal app renders, plays sound or runs correctly on the phone.

`out/tools/backup-check/backup-resources.apk` is a tiny **resource-only test**, not the game. Do not install it or present it as the deliverable. No game APK has been verified or installed.

## Resume checklist, in order

1. Establish whether the active build is still running, failed or finished. Preserve the current caches and logs. Resolve a build failure in source if needed, then run the incremental command above to include the newest committed backup integration.
2. Verify the new archive contains the actual signed game APK and both intended disc payloads. Check the generated manifest/resources, ARM64 libraries, native/ZIP alignment including 16 KiB compatibility, sensor landscape, SDK settings and backup references. Do not accept an old archive or the resource-only probe as success.
3. Check the connected Moto and any existing app/signing identity. Install/update through ADB using the matching key. Preserve existing saves; do not uninstall or clear app data as a routine workaround.
4. Test first-launch progress, successful extraction and both-disc discovery, title/new game, actual picture/movie/speaker output, restart reuse and interrupted-copy recovery. Check storage failure behavior without destructively filling the user's phone.
5. Validate visible/transparent controls, size and saved positions, optional side areas, both landscape directions, cutout/navigation insets, simultaneous fingers, very short taps, Back, hardware-controller unplugging and suspend/resume. Capture only this app's foreground content, under ignored `out/`.
6. Exercise saves, APK-update retention, representative field/battle/world-map/disc-switch scenarios and sustained performance/memory/thermal behavior. Ask the user to unlock/interact with the phone only when a concrete device check requires it.
7. Keep source changes and validation status pushed to `android-port`. Keep packages and game-dependent evidence local. The project is not ready to claim complete until the app/device gates pass.

Movie fading, automatic controller-based hiding, camera gestures, per-control scaling, landscape-side locking, save import/export and a public data-free system importer are documented follow-ups. They need not block the first complete APK feasibility check.

## Tools already installed on this machine

| Tool | Path |
| --- | --- |
| Python | `C:/Users/baldr/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe` |
| UE AutomationTool launcher | `C:/Users/baldr/ue58/Engine/Build/BatchFiles/RunUAT.bat` |
| UE bundled .NET | `C:/Users/baldr/ue58/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe` (10.0.203) |
| Visual Studio 2022 Build Tools | `C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools`; MSVC 14.44, Windows SDK 10.0.26100, .NET Framework 4.8 SDK/targeting pack |
| Android SDK | `out/tools/android-sdk` (platforms 35/36, build tools installed) |
| Android NDK | `out/tools/android-ndk-r27c` |
| Java 21 | `out/tools/java21/jdk-21.0.12.1+1` |
| ADB | `out/tools/platform-tools/adb.exe` |
| Standalone ARM64 core tests | `out/tests/android-arm64/WeltallCoreTests` |

All relative paths above are from the checkout. ADB may require elevated tool access. Native core tests use `/data/local/tmp/weltall-tests`, separately from app-private data; their cached disc copies do not prove APK extraction. Avoid rerunning the complete suite simply because a new agent starts: rerun checks when source changes or a failure justifies them.

For details see [build and packaging](AndroidBuild.md), [validation status](AndroidValidation.md), [touch controls](AndroidTouchControls.md) and [port plan](AndroidPortPlan.md). The ignored local `out/android/CONTINUE.md` points here as well.
