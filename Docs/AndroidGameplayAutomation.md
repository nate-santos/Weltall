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
