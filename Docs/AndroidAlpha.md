# Limited friend alpha.2 — October 23, 2026

The non-debuggable Shipping APK with both storage reductions is prepared locally for manual friend testing. Exact version/code/hash, installation instructions and known gaps are in [AndroidAlphaTesting.md](AndroidAlphaTesting.md); storage/package/device evidence is in [AndroidStorageProgress.md](AndroidStorageProgress.md). The general Android port remains beta.

Source `e7eae3009efb`, version `0.8.3-android-alpha.2`, code3. Local delivery: `out/releases/android-alpha.2-code3/` (APK, checksum, guide and notes only). APK989,192,294bytes,SHA-256 `0e536c5dba5339a493f733c7251146b76ea54ea05853afb789bb834647b73418`. Installed on the Moto using Files; existing saves preserved. The normal empty-slot save/cold-reload test adds Card8 No04 and advances the last-save bookmark; all pre-existing saves are unchanged.

The exact Shipping battle sample averages58.5presentedFPS, no>100msintervals, with original picture quality. Android reports1.51GBtotal on the existing test installation; private whole-disc copies are gone. First-friend fresh initialization and audible Shipping confirmation remain explicitly assigned checks because there was no second phone/profile and the Moto stayed muted. See the guide before sending. Nothing was uploaded automatically.

## Historical Moto alpha.1 fallback

### Limited Moto alpha 1 — October 21, 2026

The frozen both-disc Development build is available for private user testing on the Moto G Stylus 2025. This does not declare the full M4/M5 plan complete. Per the user's bounded-testing decision, broader progression and a normal competitive Battling match move into alpha testing instead of delaying user play. Milestone6 experiments may install a newer comparison build; the current installed identity and live tests are recorded in [AndroidHandoff.md](AndroidHandoff.md). The APK identified below remains the alpha1 fallback.

Runtime source: `8a65fbd06d3820d755a531928ea53b438a9c1824`, based on upstream 0.8.3. Documentation commits do not change this APK. Local APK: `out/android/packages/Development/20261021T204335Z-11e67ca4/Weltall-arm64.apk`. SHA-256: `33126f8be7df989bfcc7c739a0d3c2a52d01e89dcf2063b9a19a599234392221`.

## Start testing

Open Weltall on the Moto and play normally using touch or the Stadia controller. The latest checkpoint was left in a live Disc 2 Gear battle, with no automated input running. You can continue it or restart and load your preferred save. Card 8 No. 03 is the retained Disc 2 save-point checkpoint. Existing saves and control preferences are intact; media remains muted as requested. Adjust volume yourself when you want sound.

Try ordinary exploration, dialogue, character and Gear battles, menus, saving into an empty slot, and loading. Use both discs as you progress. During natural breaks, try Home/return, a landscape flip and switching between touch and the controller. These already have targeted evidence; user play should focus on whether they feel reliable during actual progression.

For an issue, note the area/save, what you pressed, what you expected, what happened, and whether it repeats. Include a screenshot if useful. Prioritize crashes, blocked progression, save problems, stuck controls, missing sound and visibly incorrect rendering. Small timing differences and disc-storage duplication are deferred.

## Test boundary and known gaps

- Focused PC arena fight, menus and winner-screen tests pass: 3 cases, 315 assertions. Earlier Moto practice/scripted arena checks are recorded. A normal competitive match/results on Android remains unverified; navigating to it is no longer a prerequisite for this limited alpha.
- The final Moto smoke uses the ordinary protected Disc 2 load, field movement, a random Gear encounter, real Home/background/return in the same processes, and responsive touch attacks afterward. It does not claim another completed battle or a new extended Disc 2 playthrough.
- Earlier evidence includes both-disc movies/audio, original disc handoff and 47-minute progression, saves/cold loads, update retention, audio interruption/recovery, physical controller reconnection, and a 30-minute performance sample. The full game has not been played through on Android. Only this Moto/Adreno GPU and the Stadia have been physically tested.
- The package is a Development build. It contains local copies of both discs and extracts working copies; optimization and broader hardware qualification belong to later work.

The APK and game data stay local in ignored folders. The repository contains source and authored documentation only. Build another private APK using the directory argument documented in the README; do not add disc images, saves or packages to Git.

See [the gameplay record](AndroidGameplayValidation.md) and [handoff](AndroidHandoff.md) for exact evidence and remaining coverage.
