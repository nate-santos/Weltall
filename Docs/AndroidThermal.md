# Android thermal and performance work

Milestone 6 starts from the limited Moto alpha, runtime source `8a65fbd`. Measure before changing rendering defaults. A cool five-minute sample does not prove equilibrium, all-area behavior, or how the phone feels in a warm room.

## First live baseline — October 21 local time

The five-minute observer finishes21 samples over300.594seconds, all in the same foreground game processes, with no reported Android/sensor throttling or observation-limit crossing. Live skin rises24.503→26.327°C; battery22–23°C; CPU29.3–37.7°C and GPU28.3–36.0°C. All53 normal player files remain byte-identical. The run begins at the ordinary Disc2 confirmation, then includes the loaded field, short normal movement, a random Gear encounter and touch attacks. Reviewed captures establish those scene changes; most of the sample is field/battle rendering rather than continuous input. This is a warm-up baseline, not equilibrium or a whole-game heat claim.

Evidence: `out/testing/thermal/20261022T022539Z-4c56ed7d-disc2-default-render` and `out/testing/android-app/thermal-default-workload`. The final recorder's separate15-second real-device metadata smoke also completes with unchanged files (`20261022T023013Z-a0e898f2-metadata-smoke`). Five parser/limit tests and11 existing reference-host tests pass. The baseline starts before the metadata enhancement; the smoke records the same settings hash `da4514e106b614bea149ee8af72369418ef913a50f32ed9168d254703143722f`, device and OS. Runtime remains the inspected alpha APK; no graphics defaults or APK changed.

The physical data connection is present, but Android reports AC-powered=true, USB-powered=false, status4/not charging,80% battery and Motorola's80% adaptive-charge limit disabled charging. Preserve these reported conditions rather than describing the run as unplugged or actively charging. Ambient temperature and case use were not measured. Media stays muted. The brief overlapping metadata smoke and initial/final captures are included overhead, so do not compare tiny temperature differences against a cleaner future run.

## Repeatable temperature recording

Open the game and verify it is actually playing, rather than showing an audio-pause panel, title prompt or background window. Then, from the repository root:

```powershell
python Scripts/android_thermal.py --seconds 600 --interval 15 --label disc2-default --home-on-limit
```

Use `--adb PATH` and `--serial SERIAL` when needed. The observer never starts/restarts the app, supplies gameplay input, changes volume, or changes graphics/settings. You can play during the observation. It records raw Android thermal/battery/window reports under ignored `out/testing/thermal/`, plus device/build identity, exact player-settings hash, player-file hashes and a summarized temperature range. The observer itself has some overhead; do not run periodic screenshots or other benchmarks alongside a comparison unless both runs do so.

Only **Current temperatures from HAL** supply CPU/GPU/battery/skin readings. The cached section can contain old values even when the phone is currently cool. Electrical-current sensors must not be treated as temperatures. Missing live readings fail the observation. Battery-service temperature is separately recorded in tenths of a degree and converted to Celsius.

Sampling stops with a nonzero result on an Android or sensor severity of MODERATE or higher, the configured skin limit, process replacement, lost foreground, transport failure or changed player-file hashes. With `--home-on-limit`, a temperature/severity limit also sends Home if the game is still foreground; it preserves the running session. The default 38°C skin limit is a conservative observation boundary matching this Moto's reported first hot-throttling skin threshold, not a universal device safety rating. The tool leaves volume and Android's thermal management untouched.

A complete report only proves that the described foreground observation finished within its limits. It cannot prove active gameplay, frame rate, comfort or thermal equilibrium by itself. Review the scene/input record and frame timings separately. Natural saves or camera preference changes can legitimately change hashes; retain that report and explain the change instead of calling it corruption or silently accepting it.

## Comparison protocol and remaining work

1. Record the APK/source, device, settings, initial live skin/battery temperatures, charging state and workload. Note room conditions and case use when known; do not infer them from a sensor.
2. Establish a 20–30 minute current-default gameplay run with active field/battle/movie portions and actual CPU/GPU frame timings. The previous 30-minute story record remains useful but did not summarize live skin temperatures.
3. Compare one change at a time with the same scene/inputs and comparable initial temperatures. Repeat in reversed order after cooling; a warmer second run alone cannot prove a regression.
4. Evaluate candidate low/default/high settings against both frame pacing and temperatures. Current Android defaults avoid MSAA and texture upscaling. Candidate lower render resolution or a 30fps presentation cap needs real rendering, input and original simulation-speed checks before becoming a user preset. High-quality MSAA/supersampling/upscaling remains unqualified on this phone.
5. Keep the existing signed alpha available while changes are evaluated. Rebuild/inspect/sign an optimized candidate only after a measured improvement, verify same-signer upgrade/save retention, and retain per-library page-alignment checks. Broader hardware qualification, disc-copy optimization and full M4/M5 gameplay coverage remain separate open work.

The custom viewport draws the game's own render passes at its chosen picture extent; engine world rendering is disabled. Do not assume Unreal's general scene screen-percentage setting reduces these passes. Check the actual render-target extents and GPU timings. Gameplay advances original vblanks from elapsed time in `FGameLoopScreen::Update`; any presentation-cap experiment must still verify simulation, movies and audio on the device.

An experimental `weltall.MobilePictureScale` console variable now provides a direct resolution comparison for those custom passes. It defaults to1.0, which calls the existing render path without an extra intermediate or upscale. Lower values are clamped to0.5–1.0, draw the game picture at reduced dimensions and bilinearly enlarge it into the original picture rectangle. Slate touch controls remain native and keep their original hit targets. This also reduces resolution of game text and effects; it is a diagnostic candidate, not a qualified user preset or new default. CSV counters `WeltallPicture/Width` and `Height` record the actual intermediate dimensions. A real PC GPU readback test covers1.0/0.75/0.5, nonzero picture origin, quadrant placement, untouched surroundings and the reduced dimensions passed to the frame. Android shader/GPU/scene/thermal validation must follow before assigning quality tiers.
