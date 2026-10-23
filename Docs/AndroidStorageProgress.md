# Android storage implementation evidence

The owner authorized implementation of [AndroidStorageReductionPlan.md](AndroidStorageReductionPlan.md), including preparation of a manually shared friend alpha. On October 22 the owner chose to retain the existing signing key for save-preserving updates. A matching private backup exists outside this Git repository, under the workspace parent's `private-signing/`; signing material must never be included in the APK delivery folder or Git.

## Completed qualification before enabling the default

Baseline installed APK SHA-256: `df991b0a7a7a35dcc8ee9910f4719a56837d9110df9978a92becaeeca9cdeb3b`, 1,573,272,787 bytes. Baseline read-only device storage and all 53 current save/settings fingerprints are under ignored `out/testing/storage/baseline/`. Preserve the current files rather than restoring older historical snapshots. Do not uninstall, clear data, overwrite occupied saves, or change preferences/volume.

The original Gradle log explicitly reported that it could not strip `libUnreal.so`. The new project-owned Gradle hook archives originals and strips merged package copies before signing. `Scripts/android_native_symbols.py` verifies each actual packaged ELF against its archived original: allocated section contents/addresses/types, GNU build ID, removed debug/static symbol sections and 4 KiB/16 KiB load alignment. The package builder also verifies offline symbol lookup, freezes the exported Git revision before building, and fails if any required archive/check is missing.

Stripped-only runtime `7c00b3904d6a8525d6e14eb021a2cb0690cbf6af`:

- APK: `out/android/packages/Development/20261022T162608Z-76ad5c1d/Weltall-arm64.apk`, 1,057,346,151 bytes; SHA-256 `c29136505424beb65daff0e9834381d0f8342e6d7fe0d2e2302b8edb0f8f2760`.
- APK reduction: 515,926,636 bytes. Expanded native-library reduction: 623,482,584 bytes. Combined APK-plus-extracted-library saving: 1,139,409,220 bytes, before disc-copy reclamation.
- Main library: 779,474,160 to 167,678,656 bytes, unchanged runtime sections and build ID `51c429a03bafac3901539a702d25e9d4679f7351`.
- Actual embedded PAK integrity/both-disc listing, signer continuity and ZIP16KiB alignment pass: `out/testing/apk/storage-strip-v1/`. Same-signer installation verifies installed hash and all 53 unchanged normal files: `out/testing/android-app/storage-strip-v1-install/`.
- Ordinary launch passes. Disc 2 movie playback passes with zero warnings/failures, 36.779 test seconds, active Android audio and expected decoded pictures/samples: `out/testing/android-app/20261022T163535Z-b6bdcf1b-storage-native/`. An isolated normal-save field/menu smoke passes and retains all files, but did not enter battle: `out/testing/android-game-reference/20261022T163731Z-1594cd41-storage-strip-battle/`.

## Direct reader and cleanup safety

`FCdImage::OpenSource` and `FGameLibrary::AddImage` share the existing sector handling, identification, completeness checks and patched-release precedence. Unreal supplies a locked random-access platform-file source. Both CUE/BIN pairs remain losslessly compressed in the existing PAK; there is no full-disc RAM cache or new persistent decompressed cache. UE already caches its most recently decompressed block; additional caching has not been justified by the measured workloads.

The reader verifies complete logical disc hashes once per manifest, then opens both discs through normal identification. The Moto's `DirectVerified.txt` matches bundle `40d10e4f99821bd39ab7a230596a3845eba5342e`. The old extracted copies stayed intact throughout comparisons. Android Assets/Mods already use stable player-data paths, so switching sources does not move them.

Cleanup validates all candidate copies before deleting any, requires matching ownership/hash markers and whole-file hashes, refuses symlink/traversal paths, removes only named files/markers, and preserves directories/unknown neighbors. It can repeat after interruption. Development comparisons can opt out of cleanup; explicit native `-Games=` inputs retain their existing path.

Evidence:

- Core media/source-opening tests: 36 cases, 21,735 assertions pass, including both local original-disc identities and 64-bit offsets: `out/testing/storage/core-media-final.txt`.
- PC Unreal storage suite: all 12 cases pass, zero warnings/failures, 0.275 test seconds: `out/testing/storage/pc-reclaim-v2/`. Earlier `pc-reclaim-v1` had one missing generated-fixture failure while another export refreshed inputs; do not refresh an export while tests consume it. The safety cases themselves passed there as well.
- Actual installed Moto storage suite: all 12 cases pass, zero warnings/failures, 0.181 test seconds, 12.375 host seconds; all 53 files retained and owned launch override removed: `out/testing/android-app/20261022T170249Z-59af42c0-storage-native/`. This includes real symlink/interrupted cleanup fixtures and concurrent reads.
- Actual PAK listing proves generated `Compressed.bin` is Oodle-compressed and `Raw.bin` uncompressed: `out/testing/apk/storage-reader-v1/pak-list.txt`. Neither fixture contains game data.
- Direct Disc 2 movie: zero warnings/failures, 36.747 test seconds, expected pictures/audio output and unchanged player files: `out/testing/android-app/20261022T164948Z-7e8c6936-storage-native/`.

## Bounded same-APK gameplay comparison

Installed comparison runtime: `3d08bee`; APK `out/android/packages/Development/20261022T165417Z-ec893dcf/Weltall-arm64.apk`, SHA-256 `bfbf9ffa53aade379ac265e226794c9c9cff9d844d787f21a88f7b53af45455e`, 1,057,563,411 bytes. Its inspection is `out/testing/apk/storage-profile-v1/`; signer/data/alignment and native symbol gates pass. Symbols are privately archived beside the APK.

`Scripts/android_storage_compare.py` runs normal isolated save loads with identical timed input, retained normal files, sampled memory/temperature, and Development-only frame/IO CSV capture. It never enables cleanup. `--resume` reuses completed matching phases instead of replaying them. The CSV analyzer resolves Unreal's appended final header and retains all numeric frames, including stalls and zero-GPU values. Initial analysis stopped on that header; the completed first phase was reused after the parser fix. Generated CSV regression tests cover this case.

Run: `out/testing/storage/comparison-20261022T170327Z-2b16cb95/`. Four 100-second normal-load references; each profiles approximately 64 seconds after initial loading, covering entry into a Gear battle and ordinary attack/menu input. Actual APK stays identical. All 15,064 numeric frames are retained, zero frames exceed 100 ms, all phases preserve the 53 normal files and remove their overrides. Final battle state matches in all four traces: actor2/menu16/end0/waits45,41,0. All four final images were visually reviewed and their PNG bytes are identical (SHA-256 `63e36b05d2313a589797ee86675cd7df246d71524fc48a80c26886b8073613d2`). CSV picture dimensions are 1028x720 in every phase, with original picture scale/partial uploads/batching retained.

| Phase | Reader | FPS | Frame p95 / p99 ms | Highest sampled PSS KiB |
| --- | --- | ---: | ---: | ---: |
| 0 | Extracted | 58.681 | 21.096 / 30.140 | 417,691 |
| 1 | Packaged | 58.856 | 20.954 / 29.550 | 405,417 |
| 2 | Extracted | 58.885 | 20.804 / 28.178 | 403,889 |
| 3 | Packaged | 59.059 | 20.564 / 27.971 | 403,956 |

Direct phases each perform 25 measured reads/1,392,384 requested bytes during the profile: combined CPU read scopes 30.653 ms and 7.909 ms respectively. This is reader CPU timing, not a physical disk/decompressor trace. These data support no meaningful regression in this workload, not a storage-induced FPS boost or universal/full-game performance claim. Existing renderer batching performance claims remain separate.

Recorded thermal data for phases1–3: skin23.878–26.436°C, no observed thermal limit. Phase0 thermal samples were not persisted by the initial failed analyzer run and must not be claimed as available. System file cache was not cleared; this is short repeated workload evidence, not sustained thermal qualification. No extra long thermal run is required under the owner's current testing preference.

## Remaining completion gates

Default reader/cleanup is enabled at `312497e`. Its Development APK installed successfully before interruption: `out/android/packages/Development/20261022T172020Z-a45d2e0e/Weltall-arm64.apk`, 1,057,569,055 bytes, SHA-256 `03ccd579f9705bba4ddc6cd4da012e5065af5c4d4201d292b4e80dbf99e8e95b`. On October 23 its ordinary launch discovers both packaged discs and removes all four verified CUE/BIN copies and associated copy markers. BundledDiscs allocation is now 15 KiB (only DirectVerified.txt remains), versus the 1,375,851 KiB baseline. All 53 save/settings hashes remain identical. Evidence: `out/testing/android-app/normal-start-storage-default-resume/`. This is the Development measurement; final Shipping footprint is still pending.

The later rescan fix `363d31f` transfers the discovered library only once, preserving discovery notes on subsequent rescans. Its Development build succeeds (`out/android/packages/Development/20261023T115754Z-c83f3f35/`); all 13 PC storage cases pass without warnings/failures in `out/testing/storage/pc-reclaim-v3/`. All 58 Python Android tests pass.

Diagnostic export opens/cancels correctly, but the first actual save exposed process-death loss: Motorola SmartBackground killed the game while its document picker was open, so its in-memory report was lost and an empty destination remained. The fix atomically stages the bounded report in private cache before opening the picker, restores it on the result after process death, and deletes the pending report after completion/cancellation. Device validation of this fix remains required. The user-reported Codex `{"detail":"Bad Request"}` was separate from the app/build.

Test Disc1 movie through direct reads and a normal relaunch/background cycle as a bounded final check. Comparison images/dimensions are fully reviewed above.

Finish the final alpha section of the plan: non-debuggable Shipping APK signed with the retained owner key, version/code and visible build identity, useful user-initiated diagnostics export, save-preserving update qualification, final exact-artifact smoke, and a local manual-delivery folder with checksum/guide/known gaps. Host release-signing configuration must use private local settings and never copy the keystore into the project/package. Diagnostic export must not include ROM/save contents or upload automatically. Only the Moto is currently available; broad device coverage moves to friends. No final Shipping artifact or distribution readiness is claimed at this checkpoint.

## October 23 resumed device qualification

`f889f34` Development builds successfully: `out/android/packages/Development/20261023T120509Z-495ab8ba/Weltall-arm64.apk`, 1,057,570,271 bytes; SHA-256 `bad35c1c2c617552c8809cf2e1ec338065be11ded186873c0718c0d3498d91f6`. Actual embedded PAK/both discs, signer continuity, native symbols and alignment pass (`out/testing/apk/storage-diagnostics-recovery-v1/`). In-place install preserves all53 files and verifies the installed APK hash (`out/testing/android-app/storage-diagnostics-recovery-v1-install/`). Native storage suite now passes13 cases, zero warnings/failures,0.231test/12.781hostseconds (`out/testing/android-app/20261023T121131Z-cbebfa4a-storage-native/`).

Diagnostic recovery passes a real process-death test: open picker, retain76,019-byte report in private cache, kill only the background app with Android's normal process manager, then save the document. The restored report has the correct build/signer,65,536-byte log tail, only the41-byte bundle verification marker, and all53 correct save/settings hashes. Pending cache is discarded and all normal files remain unchanged. Evidence `out/testing/android-app/diagnostics-recovery-v1/`. The initial verifier incorrectly trusted `adb exec-out` to propagate a remote false exit status; a subsequent shell-status check confirms the cache file is absent. This was a helper-check error, not a second export failure.

A subsequent normal launch, two-second Home/resume in the same process and diagnostic cancellation pass with all53 hashes unchanged and no recreated BIN/CUE copies (`out/testing/android-app/storage-default-lifecycle-v1/`). Picker Back may first dismiss the keyboard; cancellation was checked after actually leaving the picker. Direct Disc1 movie1 also passes:380 expected pictures, active output, zero warnings/failures,28.408test/40.578hostseconds (`out/testing/android-app/20261023T120526Z-2340874c-storage-native/`). Muted volume was not changed and no new listening confirmation is claimed.

Development diagnostic reports1,057,570,271 APK bytes plus195,459,936 extracted native-library bytes. Relative to the same baseline configuration, those components save1,139,133,644 bytes; verified CUE/BIN payload removal additionally removes1,407,439,322 logical bytes. The separately measured BundledDiscs allocation falls from1,375,851KiB to15KiB. These are scoped component measurements, not a claim about Android Settings' rounded total or final Shipping size.

The first retained-key Shipping/distribution build is running from committed `f889f34`: session7261, log `out/android/logs/Shipping-20261023T120932Z-8de8025d.log`, output `out/android/packages/Shipping/20261023T120932Z-8de8025d/`. It has a separate1136-action native engine build. Do not refresh its export or launch another builder while it runs. The tester guide draft is [AndroidAlphaTesting.md](AndroidAlphaTesting.md); exact Shipping metadata, footprint and final-artifact checks are still pending. It is not ready to send.

## Final Shipping alpha.2 — October 23, 2026

The implementation and local limited-alpha delivery preparation are complete. Fresh-device coverage and an audible Shipping check remain explicitly assigned to the first friend under the plan's unavailable-device fallback; they are not claimed as performed. The general Android port remains beta and has no full-game/broad-device qualification.

### Frozen artifact and inspection

- Source `e7eae3009efb` (full revision in package metadata), version `0.8.3-android-alpha.2`, Android code3. Package: `out/android/packages/Shipping/20261023T130659Z-07bb950c/Weltall-Android-Shipping-arm64.apk`.
- 989,192,294 bytes; SHA-256 `0e536c5dba5339a493f733c7251146b76ea54ea05853afb789bb834647b73418`.
- Retained signer SHA-256 `ee1c70153414a809b098a31a350f31d80fd1182cdaa987b97d35229c42f7ed62`. Matching private originals and offline symbolication evidence are beside that package in `native-symbols/` and `native-symbols.json`. Main BuildID `adff048062e737ee8b89537cf0dd966c93c59db5`; original484,824,640 to102,493,504 packaged bytes; all packaged native libraries remove382,940,624 expanded bytes of debug/static metadata with unchanged allocated sections/build IDs.
- The first Shipping engine build succeeded in28m55s at `f889f34`. Final repack succeeded in2m38s, using compiled caches. Final manifest removes unused Play billing/license permissions, raises code2 to3, is non-debuggable, ARM64, API31/target36, sensor-landscape, Vulkan/ASTC, and retains native extraction.
- Actual signed APK/embedded PAK passes signature continuity, integrity, both-disc listing, 4KiB/16KiB ELF geometry and ZIP16KiB alignment.32 actual cooked INIs have no signing keys/values; no keystore or generated storage fixtures is in the APK. Native reference/storage/movie automation entrypoints and the Java recreation-test action are absent. Evidence `out/testing/apk/storage-shipping-v2/`; no16KiB-page phone execution is claimed.

### Exact-artifact device checks

The owner allowed installation from Files. The APK was copied to the existing isolated Downloads test folder, located with Files and installed using Android's normal package installer. No PC installer or USB debugging is required by testers. Installed APK hash matches the final artifact; run-as correctly rejects this non-debuggable package. The normal user/profile was not cleared/uninstalled. Only that user exists; a second phone/profile was unavailable.

`out/testing/android-app/storage-shipping-v2/` holds the fresh preflight, installed/final diagnostic reports and UI evidence:

- At installation all53 prior save/settings fingerprints match exactly. Both discs open from packaged data; the only bundle file is the41-byte DirectVerified marker. Restarts/cold load do not recreate BIN/CUE copies.
- Ordinary protected Card8 No03 load reaches its Disc2 save point. Touch field movement enters a real Gear battle; ordinary touch attacks run during the60-second performance sample. The owner confirms Stadia left/right sticks, release-to-stop, X menu, D-pad and B work in this exact Shipping artifact.
- External SurfaceFlinger live-game timestamps:3,464 presented frames,60.109hostseconds,58.504FPS over the measured presentation intervals; p95=16.738ms,p99=33.410ms,max33.455ms,zero>100ms. Poll gaps max1.016s, same process. All sampled actual-present intervals are retained. This measures presentation rather than the earlier Unreal CPU-frame CSV and is a separate short Shipping smoke, not a new four-phase causal benchmark. Original picture quality remains enabled.
- Both forced landscape surfaces render and retain the process; original free/default phone rotation settings are restored. Two-second Home/resume keeps the same process. Active Android game audio has zero underruns in its recorded track. Ordinary Disc1 New Game plays the space/bridge opening movie with changing decoded images and active audio; muted volume was not changed and no fresh audible confirmation is claimed.
- Shipping save import previews and writes a generated No04 fixture into previously empty Card1 No04. Exported Card1 is131,072bytes; all three contained native hashes match its two original saves plus the fixture. That fixture is deleted through both normal confirmations. Its boot-state load is not presented as genuine story progression evidence.
- A normal save at the legal Disc2 save point writes **Card8 No04** to an explicitly confirmed free block. After a full app restart, the ordinary title loader selects No04 (51:18:20) and returns to the same field. Keep this new valid checkpoint; do not remove or overwrite it. All51 pre-existing `.sav` files remain byte-identical. LastSaved changes intentionally from108 to109 through that normal save operation. The earlier user right-stick camera test changes Settings from5898 to6075bytes (hash `95c50ed9a07a247c313cbe067038841fcd9b17d2f62a121f7e62de445932feb4`), reflecting remembered camera state. Touch idle40%,pressed30%,size100%,sides-on remain as before. Final retention reports account for these expected writes rather than claiming all53 files stayed byte-identical through intentional gameplay.
- The generated import fixture is gone; final player-file count is54 (52 native saves, LastSaved and Settings). Diagnostic reports export correctly from the non-debuggable app. Earlier Development process-death export/recovery and cancellation checks also pass; no upload is automatic.

### Final storage accounting and delivery

The exact Shipping APK is989,192,294bytes and its installed expanded libraries are103,734,304bytes:1,092,926,598 combined bytes. Android Settings independently reports rounded1.09GB app +415MB user data +98.30kB cache =1.51GB total on this existing test installation. Historical test outputs/logs are retained; this is not a fresh-install user-data claim. No private whole-disc payload remains. Report sizes after natural use can change with saves/logs/driver data.

TaskA's independent Development configuration comparison saves1,139,133,644 APK-plus-native bytes. TaskB additionally removes1,407,439,322 logical CUE/BIN bytes (plus small copy markers); actual BundledDiscs allocation falls1,375,851KiB→15KiB. Final Shipping APK-plus-native components are1,299,237,253bytes smaller than the original Development baseline; adding the removed disc payload gives2,706,676,575 component bytes saved, but that larger figure includes the separate Shipping configuration change. It is not attributed solely to symbol stripping or substituted for Android's rounded total.

Local manual-delivery folder: `out/releases/android-alpha.2-code3/`. It contains only the APK, its SHA-256 checksum, TesterGuide.md and ReleaseNotes.md. The APK copy is hash-verified against the tested artifact. No key, native symbols, standalone discs, saves or internal captures are included. The owner sends it manually; nothing is uploaded to releases. Recommend4GB free before downloading to allow staging/headroom; a tight free-space threshold was not tested.

[AndroidAlphaTesting.md](AndroidAlphaTesting.md) includes the bounded first-friend fresh-install/offline/new-game/save/restart/diagnostic check, known gaps and reporting instructions. No fresh phone wipe, another long thermal campaign or wider playthrough is claimed or required to start this limited friend alpha. Existing broader gameplay work remains in AndroidGameplayValidation.md.
