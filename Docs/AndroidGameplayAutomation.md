# Local gameplay reference automation

`Scripts/render_game_reference.py` runs a reproducible PC comparison from a native save. It automates title/Load input, optional dialogue confirmation, explicit controller actions, periodic captures and a transition summary. Use it alongside real Android play to reduce repeated dialogue checks and screenshots. A completed run still needs review of the relevant scene, movie, result or disc-change prompt before adding gameplay acceptance.

Build the `WeltallEditor` target with the current `RenderScreen` commandlet first. The default project is `out/android/project/Weltall.uproject`, prepared by the Android packager; `--project` selects another built checkout. Set `UE_ROOT` in the environment or ignored `local.env`, or pass `--ue-root`. This tool currently launches the Windows editor. It does not rebuild or install an APK.

Keep your native save, settings and action files under ignored `out/`. The `--discs` argument explicitly selects the local directory containing both BIN/CUE pairs, as with Android packaging. Disc files are read from that directory, not copied into the repository or uploaded.

```text
python Scripts/render_game_reference.py --save out/private/checkpoint.sav --discs ../bin --actions out/private/actions.json --settings out/private/Settings.txt --seconds 3600 --reader --label story-comparison
```

Use `--load-disc 2` when the checkpoint needs the second disc. The script boots with Disc 1, opens Continue/Load normally, then confirms Disc 2 when loading that checkpoint. This is saved-game loading; it does not establish the original story's Disc 1 → Disc 2 transition.

The source save is read only. A new, separate `out/testing/game-reference/<run-id>/Saves/Card1/No01.sav` contains an identical copy, so the title browser has a predictable starting slot. The tool checks both the source and that isolated load slot after play. An action that changes the isolated load slot makes the run fail; other saves created inside this isolated directory remain private test output. No phone or normal player folder is accessed. There is no game-state staging option.

## Controller actions

The JSON file contains timed actions, measured in simulated seconds from power-on. The tool reserves the first 36 seconds for ordinary startup and loading. Times and durations must be multiples of 0.1 seconds. Omitted duration means one 0.1-second press. Join simultaneous buttons with `+`; overlapping action records are rejected.

For example, these generic actions hold Right for a second and later turn the view gently:

```json
[
  { "at": 40, "buttons": "Right", "duration": 1.0 },
  { "at": 42, "buttons": "TurnRight*0.25", "duration": 0.5 }
]
```

Buttons: Up, Down, Left, Right, Cross, Circle, Square, Triangle, L1, R1, L2, R2, Start and Select. View axes: TurnLeft, TurnRight, TiltUp, TiltDown, ZoomIn and ZoomOut; an optional `*0..1` specifies their strength. `--route` and `--route-at` optionally use the commandlet's ordinary walking route. Keep checkpoint-specific action/route files local.

`--reader` presses confirm only when dialogue is waiting. It does not choose options, acknowledge battle results or answer save prompts. Add explicit inputs after reviewing those screens. A missing acknowledgement can leave a healthy game waiting; it is not evidence of a freeze.

The tool writes the dense script to `controller.txt` and uses `-PadFile`, retaining the same per-step timing as `-Pad`. Long references can therefore include late inputs without exceeding Unreal's 16,384-character command limit. `RenderScreen` rejects simultaneous `-Pad`/`-PadFile`, unreadable files and nonfinite/nonpositive step durations.

## Reviewing a run

The tool prints its private output folder and a short result. `request.json` records inputs, hashes, the actual editor DLL hashes and command. Its Git HEAD is context, not proof that an arbitrary existing binary matches that commit. `summary.json` records completion/timeout, source and isolated-save retention, logged errors, stopped programs and transitions between programs, field maps, discs and field movies. Each transition names nearby captures to review. All frames and raw logs are retained; a trace interval can miss a very short event, and nearby captures are review aids rather than proof of every intermediate frame.

Inspect `frame.png` and the transitions relevant to the intended test. Compare the same save/settings and relevant PC/Moto behavior. The summary deliberately leaves gameplay acceptance unassessed. A green process exit, a continuing dialogue scene or a long idle menu cannot establish a successful battle, full movie, original disc handoff or extended playthrough.

`--every` sets capture spacing (default 30 simulated seconds). `--audio` optionally retains a large WAV; otherwise sound advances without accumulating a recording. `--prepare-only` validates inputs and creates the isolated files without launching Unreal. The wall timeout defaults to the larger of ten minutes and three times the requested simulated duration; `--timeout` overrides it. A timeout is retained as a failed observation, not silently reported as completion or automatically restarted.

Run the generated-input checks without an engine or game data:

```text
python -m unittest discover -s Tests/Python -p "test_*.py" -v
```

## Matching on-device references

`Scripts/android_game_reference.py` runs the same normal-load script, explicit actions, dialogue reader and route on a connected Android phone. Build/install the current **Development** APK with both discs first. The shared reference code and `Weltall.Android.GameplayReference` test are excluded from Shipping builds. The tool restarts only Weltall, leaves volume alone and leaves the app stopped when finished; reopen it normally afterward.

```text
python Scripts/android_game_reference.py --save out/private/checkpoint.sav --actions out/private/actions.json --seconds 70 --every 20 --serial YOUR_ADB_SERIAL --label normal-load
```

Use `--load-disc 2`, `--reader`, `--route` and `--route-at` as with the PC runner. For Android profiling, `--every 0` omits periodic screenshot GPU readbacks while retaining the required final image and all traces; use a capture-enabled run for intermediate visual review. The phone reads its already installed discs. It uses the current phone preferences without changing them; its private output includes `player-settings.txt` for the matching PC `--settings` argument. `--prepare-only` validates and writes local inputs without accessing the phone. A single authorized device is selected automatically unless `--serial` specifies one.

Each run creates a unique `files/Weltall/Testing/GameplayReference/<run-id>` inside this app. Only an identical copy of the supplied save is placed in that run's own `Saves/Card1/No01.sav`; normal save slots and LastSaved are never used. The test powers on normally with the production renderer/audio and samples the shared input at 60 Hz, advancing only with foreground game time. Options apply only in memory, as in the PC commandlet; no settings/camera-save or import/export services are connected, and title Exit is logged. Scripted input replaces physical input during the reference, so it supplies no touch/controller acceptance by itself.

The host records the actual installed APK hashes and all normal save/settings file hashes before and after. It uploads and verifies inputs in the new test directory, uses a temporary launch override only when none exists, and removes its own override on exit. It never clears application data. The source save, staged input and isolated load slot must all remain unchanged. A completed, warning-free automation report, retained player files, final capture and trace are required for tool success. `summary.json` still leaves gameplay acceptance unassessed.

Results stay in `out/testing/android-game-reference/<run-id>`, including the native test report, `device/trace.log`, periodic game captures, final image and before/after fingerprints. The transition summary uses the same PC review format. Review relevant frames and compare the PC result before accepting a battle, movie or disc handoff. Missing prompts, timeouts, background holds and test failures are preserved, never silently retried. The default wall timeout is three times the requested duration plus three minutes.


## Actual background audio-policy denial

On Android 15 or newer, a target-SDK-35+ app without foreground status or a foreground service can have its audio-focus request rejected by the OS. The separate host-driven check exercises that real result and the production resume panel:

```text
python Scripts/android_audio_policy.py --serial YOUR_ADB_SERIAL
```

Use an installed Development APK containing the policy probe. Leave the phone unlocked, and let the tool send Home and return the game. Do not run it alongside another device test. It refuses an existing launch override, verifies unchanged normal save/settings hashes, records installed APK hashes, and leaves volume unchanged. Results remain under ignored `out/testing/audio-policy/`; the app is left stopped afterward. A unique `-WeltallAudioPolicy` identifier and private directory coordinate the native test and host. The result records the actual OS return value (`0` means failure), separate from exceptions. The Java-only Development probe delays eight seconds, verifies background status, calls the actual production audio-focus requester and records its actual state. It never broadcasts or assigns a fake denial callback. Returning in the same native processes must retain the denied hold until the real resume-panel touch obtains foreground focus; simulation/audio/input assertions then verify recovery. Development-only atomic PCM counters must also advance in two successive resumed windows beyond the initial queued buffers. An ineligible or granted background request fails the test rather than being counted as denied coverage.

This test is named separately from `Weltall.Android.AudioFocus`, whose six self-contained interruption cases still run without a host. It does not establish delayed focus while Android locks the focus stack, audible playback, every race, or alpha readiness. The probe is omitted from Shipping. See [Android's audio-focus requirements](https://developer.android.com/media/optimize/audio-focus).


The audio-policy report retains expected engine warnings. Its host verifier accepts only three explicitly reviewed messages after all native assertions pass: duplicate Android suspend notification, OpenSL's five-second wait while its hardware player is intentionally paused, and this Moto's Vulkan swapchain minimum-count message. OpenSL's platform inherits the no-op base `RequestDeviceSwap`; the paused hardware does not perform the generic worker's suggested device swap. The PCM callback assertions verify continued consumption after resume. Other warnings, errors, wrong/partial tests, changed processes or changed player files reject the run. `passed_with_reviewed_engine_warnings` distinguishes this from a warning-free native report. Earlier failed attempts and reports remain intact; do not relabel them as clean passes.
