# Android storage reduction plan

Date: 2026-10-22. Status: implementation authorized by the owner's subsequent request to follow this plan. Acceptance gates below remain required.

## Objective and boundaries

Reduce the game's total installed footprint through two independent changes:

1. Strip debug and static symbol metadata from packaged native libraries while retaining matching symbols on the development PC.
2. Read both discs from the installed package rather than maintaining full extracted BIN copies in private app storage.

Both discs must remain available offline. Preserve original sector contents, gameplay, image quality, audio, controls, save compatibility and the current rendering optimizations. Gameplay performance must remain equivalent or better in representative on-device comparisons before either change becomes the default. This is an acceptance requirement, not an assumption that compressed reads are free.

This plan does not include CHD conversion, cross-disc deduplication, lossy asset changes, removal of content, native-library direct APK loading, or changes to the user's disc-import workflow. Keep `--include-discs FOLDER` accepting the existing local CUE/BIN pairs. Native direct loading can be evaluated separately later, without double-counting its savings.

## Known baseline and savings estimates

The inspected Development APK contains runtime commit `5d6c4080041e6d44ccf5bd42cd652e16c83a1293`, with partial VRAM uploads and translucent batching enabled. Its local archive is `out/android/packages/Development/20261022T123910Z-210755e3/Weltall-arm64.apk`.

| Component | Observed bytes | Implication |
| --- | ---: | --- |
| APK | 1,573,272,787 | Includes compressed native libraries and the embedded game PAK. |
| Expanded `libUnreal.so` | 779,474,160 | Contains substantial non-loaded metadata. |
| Non-loaded debug/static symbol metadata in that library | 611,794,582 | Section-size estimate of removable data; actual stripped output has not been produced. |
| All expanded native libraries | 818,891,064 | Current manifest requests native extraction. |
| Both original BINs | 1,407,439,152 | Currently duplicated into private storage for native file access. |
| Both BINs stored inside the PAK | 944,202,239 | Already compressed; simply enabling compression is not a new saving. |

Stripping could reduce the expanded main library to approximately 168 MB, plus reduce its compressed APK entry by an amount that must be measured. Direct reads from the existing compressed bundle could remove approximately 1.41 GB of private disc data, less any small persistent metadata. These are decimal units and estimates, not measured final installation savings.

If direct compressed reads cannot meet performance requirements, storing only the bundled disc entries uncompressed would grow the current PAK by approximately 463 MB before overhead, but still remove the 1.41 GB extracted copy: approximately 944 MB net disc-storage savings. This is the performance-first fallback, subject to measuring the actual artifact. Do not uncompress unrelated content or retain a permanent second disc copy.

The approximate sum of APK, extracted libraries and extracted BINs is 3.80 GB before saves, ART and other caches. Establish fresh device measurements rather than treating this sum as an Android Settings reading.

## Phase 0: capture a comparable baseline

- Record the current source revision, APK hash, signing identity, native build IDs, ABI, build configuration, ZIP compression methods and manifest extraction setting. Inspect the actual APK, not only intermediate files.
- Record APK bytes, expanded native-library bytes, app-private disc bytes, other app data and total installed usage where accessible. Distinguish logical file lengths from allocated storage and record Android's reported app/data/cache figures separately.
- Snapshot the current save/settings files and hashes immediately before testing. Historical reference: 53 normal player files, including 51 saves and occupied Card 8 slots 02/03. Discover the current set; never overwrite newer user progress with that historical baseline.
- Preserve signing identity and update in place. Do not uninstall, clear app data, overwrite occupied saves, or alter volume/preferences. Use an isolated test installation/device for destructive or clean-install tests; a fresh test namespace can cover storage initialization on the Moto without clearing normal data.
- Keep ROMs, extracted sectors, symbols, APKs, traces, logs, screenshots and player-data backups outside Git under ignored `out/` or the external disc directory. Synthetic fixtures alone belong in committed tests.

## Task A: strip packaged native libraries

### A1. Locate and correct the final packaging step

Inspect `Scripts/android_package.py`, the exported project's Android staging/Gradle configuration and the relevant UE Android build steps. The existing `-nodebuginfo` argument has not prevented debug metadata from reaching the actual APK. Find whether stripping is disabled, bypassed or undone by a later copy.

Prefer a supported build/packaging setting that reliably strips final staged libraries. If that is insufficient, add an explicit, repeatable staging step using the matching Android NDK LLVM tools. Operate on the package copies before APK alignment/signing, never on the only unstripped build output. Apply the policy to Development and Shipping packages without changing build configuration merely to obtain smaller binaries.

Archive each library's original symbols first, keyed by source revision, ABI and build ID. Retain the current `Weltall_Symbols_v1` workflow or an equivalent discoverable archive. Stripping must preserve loadable sections, required dynamic symbols/relocations, exception and unwind information, JNI entry points, library dependencies and build IDs. Do not blindly remove all symbol-related sections or strip third-party libraries without checking their runtime requirements.

### A2. Make verification part of packaging

- Inspect every native library inside the finished APK. Assert the targeted debug/static-symbol sections are absent and report before/after sizes; tolerate metadata that has a demonstrated runtime requirement.
- Compare loadable segment contents and runtime linking metadata against the matching pre-strip binaries. Explain any tool-induced layout differences; require executable code and runtime data equivalence.
- Verify all required shared libraries, signing identity, ABI and existing 4 KiB/16 KiB ELF/ZIP alignment checks. Preserve both embedded discs and their content hashes.
- Verify a known native address can still be resolved using the archived matching symbols. Use an isolated crash fixture only if needed; do not crash a normal player session just to validate symbol lookup.
- Fail packaging if stripping did not actually happen or matching symbols were not archived. Never report a smaller intermediate library as proof that the installed APK is smaller.

### A3. Validate independently

Install the stripped-only build as an update. Verify save/settings retention, startup, a short battle, a movie and audio. Compare a bounded matched battle sample against the current build with the same renderer settings. No meaningful runtime change is expected because removed sections are not loaded, but verify native startup/linking and crash symbolication before proceeding.

Record real APK and installed-native savings. Keep this as an independently reviewable change and baseline for Task B.

## Task B: read bundled discs without extracting them

### B1. Separate disc identity from native filesystem paths

Current integration points:

- `Source/WeltallCore/Public/Foundation/ByteSource.h`: `IByteSource::Size()` and random-access `Read()`.
- `Source/WeltallCore/Public/Media/CdImage.h` and its implementation: accepts an `IByteSource` for a data track, while file discovery/CUE opening currently use native paths.
- `Source/WeltallCore/Public/Media/GameLibrary.h` and its implementation: filesystem scanning, identification and disc selection.
- `Source/Weltall/Private/Shell/GameDiscsSubsystem.cpp`: preparation and library scanning.
- `Source/Weltall/Private/Shell/BundledDiscsPreparation.cpp`: manifest validation and the current extraction workflow.
- `Scripts/android_package.py`: CUE/BIN staging, manifest generation and UFS packaging.

Introduce the smallest source-based discovery/opening interface needed to supply packaged disc descriptors to the existing identification and game-library logic. Reuse release detection, sector handling and patched-release precedence. Retain filesystem loading for desktop, explicit `-Games=` overrides and existing external-disc use.

Resolve bundled CUE references through the manifest and packaged-file provider. Preserve supported track offsets, sector sizes and filenames; reject malformed or out-of-bundle paths. Do not reinterpret raw 2352-byte sectors as 2048-byte ISO sectors, which would discard XA/movie information.

Audit consumers of `GamesFolder()` and `FileOf()`. In particular, `GameAssetsSubsystem` and `GameModsSubsystem` currently derive writable asset/mod folders from the games-folder parent. Keep these writable locations and existing user content stable rather than redirecting them into the read-only package. Preserve diagnostics and disc identity without requiring a fake native BIN path.

### B2. Implement packaged random-access reads at the Unreal boundary

Provide an Unreal-layer `IByteSource` adapter over the mounted platform-file/PAK system. Keep Unreal dependencies out of WeltallCore. Use existing PAK decompression instead of writing a separate PAK or Oodle reader.

The adapter must own valid handles for the disc's lifetime, use 64-bit sizes/offsets, check ranges without overflow and satisfy the existing all-bytes-or-failure read contract. Preserve seek/read correctness under concurrent callers using independent handles or appropriate synchronization; never expose a shared mutable seek position unsafely. Handle shutdown, background/resume, reopening and errors without use-after-close or silently substituted sector data.

Open both discs from the package, identify them through the normal core logic and retain availability for switching. Replace copy-progress UI with accurate opening/verification status where needed. Do not require free space for another complete BIN pair or read the entire pair into RAM.

### B3. Bound decompression and caching costs

First inspect the actual PAK compression-block size and Unreal's existing buffering/cache behavior. Instrument requested bytes, underlying reads/decompressions, latency, cache hits/misses and peak cache memory in diagnostic builds. Avoid duplicating an effective engine cache without evidence.

If necessary, introduce a bounded block cache aligned to the actual compressed-read granularity, shared within a fixed total budget across both discs. Prototype a small budget, for example 16 MiB total, and tune from measured workloads. Cache keys must include bundle/disc identity and block offset; invalidate on package changes. Any prefetch must be bounded, cancelable and unable to starve immediate reads. No full-disc memory cache, unbounded growth or permanent on-disk decompressed cache.

Preferred candidate: existing losslessly compressed PAK entries plus bounded buffering. Performance fallback: selectively store disc entries uncompressed and read them in place through the same adapter. Verify outer APK storage does not reintroduce compression requiring full extraction. Compare actual total storage, not APK size alone. If neither candidate meets performance and storage requirements, retain the existing default and report the remaining blocker rather than accepting slower gameplay.

### B4. Integrity and upgrade migration

Preserve the manifest's content-identity contract. Verify complete logical disc bytes against the local originals during package qualification through the new reader. For runtime use, choose explicit integrity handling: a one-time streaming check per new bundle or verified per-block hashes generated while staging. Do not scan 1.41 GB synchronously every launch. If block verification is used, validate requested blocks before serving them, include its cost in the benchmark and treat the signed packaged manifest as authoritative. Keep verification state small and keyed by bundle identity.

For upgrades, start with the existing extracted discs intact while qualifying the direct reader. Before reclaiming them, validate both packaged discs, normal identification and save-loading compatibility. Production cleanup must only remove positively identified app-generated disc copies and preparation markers within the owned `BundledDiscs/<version>/Games` subtree; preserve sibling Assets/Mods folders, unknown files and all player data. Resolve and verify target paths and refuse symlink/path escapes. Never recursively delete the entire player-data or version root.

Make cleanup resumable/idempotent, including interrupted extraction leftovers. If validation fails, retain the old copies and give an actionable error; no silent deletion or permanent storage duplication disguised as success. Verify a second launch does not recreate BINs. Bundle updates and interrupted migration must reopen correctly without confusing old verification markers with the new package.

Keep the old reader available in development for comparisons and recovery until the new path passes. After cleanup, a code rollback to the extracting reader may need to re-extract discs and require free space; do not promise that rollback is storage-free. Save/settings compatibility must survive either path.

## Validation and acceptance gates

### Correctness tests

Use synthetic fixtures for unit tests covering first/last bytes, empty and out-of-range reads, compression-block boundaries, sector-crossing reads, offsets beyond 32 bits, concurrent reads, malformed CUE/manifest input, missing/truncated/corrupt sources, cancellation and reopened handles. Exercise raw Form 1/Form 2/XA sectors and track offsets. Reuse existing CD-image/game-library behavior tests.

Add packaged Unreal tests that actually read mounted compressed and uncompressed fixture entries, not just a filesystem imitation. Locally compare both real discs end-to-end through the new adapter against original hashes without copying game data into the repository. Test migration using isolated fixtures: complete copies, partial copies, older versions, unknown neighboring files and interrupted cleanup.

On the Moto, check Disc 1 and Disc 2 loading, disc selection/switching, field transitions, a bounded active battle/effect sequence, each disc's movie/audio playback, and background/resume/relaunch. Preserve touch/controller behavior and saves. Ask for audible confirmation only when needed; do not change the user's muted volume.

### Performance comparisons

Use the stripped-only build with extracted discs as the Task B baseline. Prefer a development-only reader selector in the same candidate binary so direct and extracted paths share identical game/rendering code. Retain baseline copies only for this temporary comparison; final storage measurements occur after their removal.

Compare an A/B/A/B sequence of short matched workloads, with approximately 60 seconds per steady battle phase and bounded field/movie/read workloads. Include cold application starts and repeat warm reads; restarting the app does not clear Android's system file cache, so label cache conditions honestly. Use isolated device reboot or a distinct workload only if a material cache-dependent result remains unresolved.

Measure average FPS, p95/p99 frame times, visible stalls, audio underruns, scene/load latency, CPU work and peak resident/cache memory. Retain all valid frames, including stalls. Test active I/O and transitions as well as stationary battle menus; unchanged menu FPS alone cannot qualify a disc reader. Keep original picture scale, partial uploads and batching enabled and record actual rendered dimensions.

Suggested investigation triggers are a repeated FPS drop over 2%, p95/p99 increase over 5%, new frames over 100 ms attributable to reads, or repeated load-time degradation over 5%. These are triage thresholds, not permission to knowingly ship a smaller regression. Do not chase isolated timing noise; resolve reproducible changes with one focused repeat. Acceptance requires no meaningful repeatable gameplay or loading regression, no new audio glitches and bounded memory use without pressure-induced kills. Report the tested device/workload limits rather than claiming universal performance.

No dedicated long thermal run is required for this work. Observe temperature/throttling during the short comparisons and revisit sustained testing only if the new read/decompression workload produces a material concern, consistent with the current project decision.

### Storage and retention gates

- Stripped package contains no unnecessary targeted debug/static symbol payload; matching offline symbols remain usable.
- Both discs work offline without native BIN duplicates on a clean initialization and after upgrade migration.
- Measure final APK plus installed native code plus private data/cache after the same gameplay workload, on first and subsequent launches. Count any new cache against savings and distinguish temporary update space from steady installed usage.
- Show each task's independent and combined byte savings. Do not add savings calculated from incompatible packaging configurations.
- Confirm all pre-existing saves/settings and user assets remain unchanged except deliberate test writes in isolated locations. Verify cleanup does not expand into neighboring directories.
- Failed integrity/read/migration cases remain recoverable and cannot silently produce incorrect game data.

## Delivery order and completion report

1. Capture baseline and implement/qualify symbol stripping independently.
2. Add source-based packaged disc opening and synthetic/package reader tests.
3. Benchmark compressed direct reads; add/tune bounded caching only as needed. Use the uncompressed-disc fallback if it better satisfies performance requirements.
4. Qualify safe upgrade migration and cleanup after direct reading passes.
5. Measure the combined final installation and update build documentation/handoff with exact configuration, symbols location, measured savings, performance evidence and remaining device coverage limits.

When implementation is later authorized, keep these as small reviewable commits on the Android branch and use the existing repository guard before pushing. Build artifacts and test evidence stay local. Completion means a measured smaller installed game with both discs available, preserved player data and no meaningful performance regression in the bounded acceptance suite. Writing this plan does not authorize beginning those implementation steps.

## Final phase: prepare the APK for manually distributed alpha testing

After both storage changes pass, prepare one self-contained APK that the project owner can send directly to friends. This is a private sideloaded alpha; no Play Store submission, app bundle, store asset delivery, tester account system or automatic updater is required. Preparation ends with a verified local release folder and instructions; the owner sends the files manually.

### Final packaging and build identity

- Produce a Shipping candidate through the existing `--configuration Shipping --include-discs FOLDER` workflow, with both discs inside the APK and the qualified direct reader enabled. Confirm the new stripping step also runs for Shipping. Keep matching unstripped symbols privately on the PC.
- Verify Shipping-specific behavior rather than treating Development tests as proof: cooked shaders/content, disc discovery, save import/export, audio, controls, lifecycle recovery and the new reader must work without developer-only code. Disable automation input, reference runners, profiling overlays, console shortcuts and test launch overrides in the distributed build. Retain the project's existing Shipping logging support with bounded log retention.
- Inspect the final manifest and package: non-debuggable, intended ARM64 ABI and Vulkan support, both landscape orientations, required permissions only, all needed resources and both discs present. Preserve the qualified native extraction setting; changing to direct native-library loading is outside this plan.
- Assign a clear version such as `0.8.3-android-alpha.2` and a monotonically increasing Android version code. The exact label should reflect the eventual source baseline. Make the version/build identifier accessible in the game so testers can report it without developer tools. Keep the package name stable for subsequent alpha updates.
- Keep existing beta wording in general project documentation if appropriate, but describe this particular distribution as a limited friend-testing alpha with its own build identity and known issues. Do not imply complete playthrough or broad device qualification.

### Signing and update continuity

Use a durable, owner-controlled signing identity for all friend-test APKs. Inspect the existing installed build's certificate before choosing the key: if it is suitable and available, preserving it permits updates in place. Otherwise, establish the alpha signing key before the first friend distribution and explicitly plan migration for any existing differently signed installation. Do not try to solve a signature mismatch by uninstalling the user's current game.

Keep the keystore, credentials and a recoverable backup outside Git and outside the tester delivery folder. Signing credentials must not appear in command logs or committed configuration. Verify the completed APK signature and record its certificate fingerprint. Test an update between two alpha version codes signed with the chosen key and confirm saves/settings survive. Once friends have installed an alpha, retain that package name and signing identity for future updates.

If the owner's current install needs a different signing identity, validate the existing save export/import workflow and separately account for settings before any migration. Such migration is a distinct step; it must not be hidden inside storage cleanup or performed automatically as part of this planning task.

### Bounded qualification of the exact distribution APK

Run a short smoke test on the exact signed Shipping artifact that will be sent, not an earlier Development binary. Cover first launch without previously extracted BINs, both-disc loading, a brief battle, movie/audio playback, touch/controller input, landscape rotation, background/resume, and save/load in isolated slots. Check that a restart does not recreate disc copies and that the final installed footprint matches the storage report.

Validate normal installation from a downloaded APK using the phone's package installer, without ADB or a PC being required by the tester. Use a separate test device or isolated Android user/profile if available for a genuine fresh install; never clear the Moto's normal player data to simulate one. If that environment is unavailable, distinguish the tested isolated initialization from the still-unverified fresh-install path and arrange a bounded first-tester installation check.

Repeat a short representative performance check on Shipping, since its compilation and packaging differ from Development. No additional sustained thermal campaign is a prerequisite. Document that the Moto is the currently tested device, and use friend testing to broaden coverage rather than promising compatibility with every ARM64 phone.

### Tester instructions and useful reports

Include a short tester guide alongside the APK with:

- The build label, APK SHA-256, measured download size and measured free-space recommendation covering installation/update overhead. State the actual packaged minimum Android version (currently API 31/Android 12), ARM64 and graphics requirements verified from the final build, plus tested phone models.
- Steps to download the complete APK, allow installation from the particular browser/file-sharing app when Android requests it, install, and launch. Explain that the APK includes both discs and works offline, with no separate disc-copy setup. USB debugging is unnecessary for normal testing.
- Update instructions: install the newer APK over the existing app, keep saves/settings, and report signature/version errors rather than uninstalling. Explain save export before any optional reinstall and that uninstalling removes private app data.
- A quick guide to transparent touch controls, the Controls/settings menu, landscape rotation, controller pairing and mixed touch/controller use. Let testers choose their own opacity and volume.
- A short checklist focused on normal play: exploration, dialogue, character/Gear battles, saving/loading, both discs as progression permits, movies, interruption/resume and controller disconnect/reconnect. List known issues and coverage gaps; do not make friends repeat every automated check.
- A simple report template: build label, phone model, Android version, controller/headphones if relevant, area/disc, reproduction steps, expected/actual result and whether it repeats. Ask for screenshots or a short clip when useful; prioritize crashes, lost saves, blocked progress, audio faults and repeatable stalls.

Because a non-debuggable build cannot depend on developer access to private files, verify an accessible way to share diagnostics. If none exists, add a small user-initiated diagnostic export using Android's document/share flow, with bounded logs and build/device details. Exclude ROM bytes, save contents and unrelated private files by default. Do not add background uploading, accounts or broad storage permissions. Retain private matching symbols so returned crash addresses can be interpreted for that exact build.

### Local delivery folder and final checklist

Create an ignored release folder containing only the signed APK, its checksum, the tester guide and concise release notes/known issues. Exclude signing keys, native symbols, standalone ROMs, internal test captures, developer settings and player saves. Keep the both-disc APK local rather than committing it to Git or automatically publishing it to repository releases.

Record the final source revision, version name/code, APK hash, signing fingerprint, symbol archive location, measured installed size and exact validation results in the internal handoff. Update `Docs/AndroidBuild.md` and the alpha notes to describe the new storage behavior and manual update procedure, removing obsolete instructions that require first-run BIN extraction. The final readiness decision is based on this exact artifact: signed and installable, both discs available offline, no unnecessary debug metadata or extracted disc duplicates, preserved saves, useful diagnostics and no meaningful gameplay performance regression in the bounded tests. Sending the APK to friends remains the owner's manual step.

## Implementation outcome — October 23, 2026

The authorized implementation and local private-alpha packaging are finished; exact artifacts, independent savings and test evidence are recorded in [AndroidStorageProgress.md](AndroidStorageProgress.md). This outcome supersedes the original planning-only wording above.

- [x] Strip package-copy native debug/static metadata, retain matching private symbols and verify unchanged runtime sections/build IDs, offline lookup and alignment, including Shipping.
- [x] Read both losslessly compressed packaged discs directly through shared identification/sector logic; verify real logical contents once per bundle without a second persistent full-disc cache.
- [x] Qualify guarded, resumable copy reclamation with PC/Moto fixtures and the real upgrade; preserve original saves and unknown neighboring files.
- [x] Complete bounded same-APK A/B/A/B battle comparison and final Shipping presentation smoke without meaningful regression or quality reduction.
- [x] Produce retained-key, non-debuggable Shipping alpha.2/code3; qualify actual package/content, normal Files installation and version2→3 save-preserving update.
- [x] Validate ordinary loading, native save to an empty slot and cold reload, import/export, touch/controller input, landscape surfaces, movies/active audio, resume and diagnostics. Account explicitly for the new test save, its bookmark and user camera writes.
- [x] Record final installed footprint; update build/alpha/handoff docs and prepare a hash-verified local APK/checksum/guide/release-notes folder with no private keys/symbols or unrelated data.
- [ ] First friend fresh-device launch/offline/save/restart check: no second phone or isolated user was available, so use the expressly allowed first-tester fallback in [AndroidAlphaTesting.md](AndroidAlphaTesting.md). Normal Moto data was never wiped.
- [ ] Fresh subjective Shipping audio confirmation: the owner kept the Moto muted. Active audio output and movie progression are verified; the guide asks the first tester to listen at their chosen volume. Earlier Development both-disc listening confirmations remain historical evidence.

The owner manually sending the APK and broader phone/playthrough coverage are subsequent alpha activity. Neither storage task requires additional implementation work at this checkpoint.
