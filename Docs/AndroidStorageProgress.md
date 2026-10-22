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

Run: `out/testing/storage/comparison-20261022T170327Z-2b16cb95/`. Four 100-second normal-load references; each profiles approximately 64 seconds after initial loading, covering entry into a Gear battle and ordinary attack/menu input. Actual APK stays identical. All 15,064 numeric frames are retained, zero frames exceed 100 ms, all phases preserve the 53 normal files and remove their overrides. Final battle state matches in all four traces: actor2/menu16/end0/waits45,41,0; final image was reviewed for the first phase and the remaining images still require comparison review before final reporting.

| Phase | Reader | FPS | Frame p95 / p99 ms | Highest sampled PSS KiB |
| --- | --- | ---: | ---: | ---: |
| 0 | Extracted | 58.681 | 21.096 / 30.140 | 417,691 |
| 1 | Packaged | 58.856 | 20.954 / 29.550 | 405,417 |
| 2 | Extracted | 58.885 | 20.804 / 28.178 | 403,889 |
| 3 | Packaged | 59.059 | 20.564 / 27.971 | 403,956 |

Direct phases each perform 25 measured reads/1,392,384 requested bytes during the profile: combined CPU read scopes 30.653 ms and 7.909 ms respectively. This is reader CPU timing, not a physical disk/decompressor trace. These data support no meaningful regression in this workload, not a storage-induced FPS boost or universal/full-game performance claim. Existing renderer batching performance claims remain separate.

Recorded thermal data for phases1–3: skin23.878–26.436°C, no observed thermal limit. Phase0 thermal samples were not persisted by the initial failed analyzer run and must not be claimed as available. System file cache was not cleared; this is short repeated workload evidence, not sustained thermal qualification. No extra long thermal run is required under the owner's current testing preference.

## Remaining completion gates

Enable the qualified reader/verified cleanup default, build and test the resulting real migration, measure actual private storage reclamation, and keep all player fingerprints intact. Test Disc1 movie through direct reads and a normal relaunch/background cycle as a bounded final check. Review remaining comparison images/dimensions.

Finish the final alpha section of the plan: non-debuggable Shipping APK signed with the retained owner key, version/code and visible build identity, useful user-initiated diagnostics export, save-preserving update qualification, final exact-artifact smoke, and a local manual-delivery folder with checksum/guide/known gaps. Host release-signing configuration must use private local settings and never copy the keystore into the project/package. Diagnostic export must not include ROM/save contents or upload automatically. Only the Moto is currently available; broad device coverage moves to friends. No final Shipping artifact or distribution readiness is claimed at this checkpoint.
