"""Play an isolated PC reference from an unchanged native save and local inputs.

Inputs, controller scripts, captures, logs and reports stay in ignored out/.
The summary identifies transitions to review; it never grants gameplay acceptance.
Requires a built editor with RenderScreen's -PadFile support. No phone is changed.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid

from android_package import REPO, PROJECT, disc_inputs, setting


BUTTONS = {"Up", "Down", "Left", "Right", "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "Start", "Select"}
AXES = {"TurnLeft", "TurnRight", "TiltUp", "TiltDown", "ZoomIn", "ZoomOut"}
STEP = 0.1


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        while block := source.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def step_number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError("action times must be finite numbers")
    result = round(value / STEP)
    if abs(result * STEP - value) > 1e-7:
        raise ValueError("action times and durations must be multiples of 0.1 seconds")
    return result


def pad_script(seconds, actions, load_disc):
    """Normal title/Load input followed by explicit actions; no state staging."""
    steps = [""] * step_number(seconds)
    for at in (20, 22, 24, 26, 29, 31):
        steps[step_number(at)] = "Cross"
    steps[step_number(28)] = "Left"
    if load_disc == 2:
        for at in (33, 35):
            steps[step_number(at)] = "Cross"
    if not isinstance(actions, list):
        raise ValueError("the actions file must contain a JSON array")
    occupied = set()
    for action in actions:
        if not isinstance(action, dict) or set(action) - {"at", "buttons", "duration"} or not {"at", "buttons"} <= action.keys():
            raise ValueError("each action needs at/buttons and optionally duration")
        start = step_number(action["at"])
        length = step_number(action.get("duration", STEP))
        if start < step_number(36) or length < 1 or start + length > len(steps):
            raise ValueError("actions must start at/after 36 seconds and finish within the reference")
        if not isinstance(action["buttons"], str) or not action["buttons"]:
            raise ValueError("an action must name at least one button or view axis")
        for button in action["buttons"].split("+"):
            name, separator, amount = button.partition("*")
            if name not in BUTTONS | AXES or (separator and name not in AXES):
                raise ValueError(f"unknown controller action: {button}")
            if separator and (not math.isfinite(float(amount)) or not 0 <= float(amount) <= 1):
                raise ValueError("view-axis amounts must be between zero and one")
        indexes = set(range(start, start + length))
        if occupied & indexes:
            raise ValueError("controller actions overlap; join simultaneous buttons with '+'")
        occupied |= indexes
        steps[start:start + length] = [action["buttons"]] * length
    while steps and not steps[-1]:
        steps.pop()
    return ",".join(steps)


def summarize_log(path, folder):
    transitions = []
    previous = {}
    elapsed = None
    errors = []
    for line in path.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        tick = re.search(r"GameTrace ([\d.]+) stage=(\d+) program=(\d+)", line)
        if tick:
            elapsed = float(tick[1])
            kind, state = "program", {"stage": int(tick[2]), "program": int(tick[3])}
        else:
            field = re.search(r"GameTrace field map=(\d+).* disc=(\d+) movie=(-?\d+)", line)
            if field:
                kind, state = "field", {"map": int(field[1]), "disc": int(field[2]), "movie": int(field[3])}
            else:
                if re.search(r"\b(?:Error|Fatal):|Ensure condition failed", line):
                    errors.append(line)
                continue
        if state != previous.get(kind):
            transitions.append({"at_seconds": elapsed, "kind": kind, **state})
            previous[kind] = state
    frames = sorted((float(match[1]), file.name) for file in folder.glob("frame-*.png")
                    if (match := re.fullmatch(r"frame-([\d.]+)\.png", file.name)))
    for transition in transitions:
        at = transition["at_seconds"]
        before = [name for moment, name in frames if moment <= at]
        after = [name for moment, name in frames if moment >= at]
        transition["review_before"] = before[-1] if before else None
        transition["review_after"] = after[0] if after else "frame.png"
    return {"last_trace_seconds": elapsed, "transitions": transitions, "logged_errors": errors,
            "stopped_program": any(item.get("stage") == 3 for item in transitions),
            "visual_review_required": True, "gameplay_acceptance": "not assessed"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--save", type=Path, required=True, help="existing 7,936-byte native .sav; read only")
    parser.add_argument("--discs", type=Path, required=True, help="local directory containing both BIN/CUE pairs")
    parser.add_argument("--actions", type=Path, help="JSON at/buttons/duration actions, starting at second 36")
    parser.add_argument("--settings", type=Path, help="native Settings.txt to apply without writing player preferences")
    parser.add_argument("--load-disc", type=int, choices=(1, 2), default=1)
    parser.add_argument("--seconds", type=int, default=3600)
    parser.add_argument("--every", type=float, default=30, help="capture interval in simulated seconds")
    parser.add_argument("--size", default="1600x1200")
    parser.add_argument("--reader", action="store_true", help="confirm waiting dialogue; not choices, saves or battle results")
    parser.add_argument("--battle-driver", choices=("StoreCombo",), help="Development-only on-foot menu input; saves AP, then uses Combo")
    parser.add_argument("--route", help="normal field walking route in RenderScreen x,z;!x,z format")
    parser.add_argument("--route-at", type=float, default=36)
    parser.add_argument("--audio", action="store_true", help="retain a potentially large WAV; otherwise sound still advances")
    parser.add_argument("--ue-root", type=Path)
    parser.add_argument("--project", type=Path, default=PROJECT / "Weltall.uproject")
    parser.add_argument("--label", default="story")
    parser.add_argument("--timeout", type=float, help="wall seconds; default max(600, 3 x simulated seconds)")
    parser.add_argument("--prepare-only", action="store_true", help="validate inputs/write the isolated script without running Unreal")
    args = parser.parse_args()
    if not 40 <= args.seconds <= 7200 or not math.isfinite(args.every) or not .1 <= args.every <= 60:
        raise ValueError("use 40–7,200 seconds and a finite capture interval from 0.1 to 60 seconds")
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", args.label) or not re.fullmatch(r"\d+x\d+", args.size):
        raise ValueError("use a short letters/digits/dashes label and a WIDTHxHEIGHT size")
    if any(not 320 <= int(value) <= 4096 for value in args.size.split("x")):
        raise ValueError("image dimensions must be between 320 and 4,096")
    if not math.isfinite(args.route_at) or not 36 <= args.route_at < args.seconds:
        raise ValueError("route start must be at/after 36 seconds and before the reference ends")
    timeout = args.timeout if args.timeout is not None else max(600, args.seconds * 3)
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("timeout must be finite and positive")
    data = args.save.read_bytes()
    if len(data) != 7936 or sum(data[:-1]) % 256 != data[-1]:
        raise ValueError("the save must be a checksum-valid 7,936-byte native save")
    disc_inputs(args.discs)
    actions = json.loads(args.actions.read_text(encoding="utf-8-sig")) if args.actions else []
    script = pad_script(args.seconds, actions, args.load_disc)
    engine = args.ue_root or (Path(value) if (value := setting("UE_ROOT")) else None)
    if engine is None:
        raise ValueError("set UE_ROOT or supply --ue-root")
    editor = engine / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
    project = args.project.resolve()
    if not editor.is_file() or not project.is_file():
        raise ValueError("the Windows editor and built project must exist")
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8] + "-" + args.label
    folder = (REPO / "out/testing/game-reference" / run_id).resolve()
    if not folder.is_relative_to(REPO.resolve() / "out"):
        raise ValueError("reference output must stay inside this checkout's ignored out directory")
    folder.mkdir(parents=True, exist_ok=False)
    copied = folder / "Saves/Card1/No01.sav"
    copied.parent.mkdir(parents=True)
    copied.write_bytes(data)
    pad = folder / "controller.txt"
    pad.write_text(script, encoding="utf-8")
    command = [str(editor), str(project), "-run=RenderScreen", "-AllowCommandletRendering", "-Screen=Game",
               f"-Games={args.discs.resolve()}", "-Disc=1", f"-Seconds={args.seconds}", f"-Every={args.every}",
               f"-Size={args.size}", f"-PadFile={pad}", f"-PadStep={STEP}", "-GameTrace=2", f"-Saves={folder/'Saves'}",
               f"-Out={folder/'frame.png'}", f"-abslog={folder/'editor.log'}", "-RenderOffscreen", "-unattended", "-NoSound", "-nosplash", "-NoP4"]
    if args.reader:
        command.append("-Reader=0.5,0.02")
    if args.battle_driver:
        command.append(f"-BattleDriver={args.battle_driver}")
    if args.route:
        command.extend([f"-Route={args.route}", f"-RouteAt={args.route_at}", "-RouteBattles"])
    if args.audio:
        command.append(f"-SoundOut={folder/'audio.wav'}")
    if args.settings:
        lines = [line.strip() for line in args.settings.read_text(encoding="utf-8-sig").splitlines()
                 if line.strip() and not line.lstrip().startswith("#")]
        command.append("-Setting=" + ";".join(lines))
    if len(subprocess.list2cmdline(command)) >= 16000:
        raise ValueError("the remaining command line exceeds Unreal's limit")
    dlls = sorted((project.parent / "Binaries/Win64").glob("UnrealEditor-Weltall*.dll"))
    if not dlls:
        raise ValueError("build the project's editor modules first")
    request = {"command": command, "actions": actions, "source_save": str(args.save.resolve()), "source_save_sha256": hashlib.sha256(data).hexdigest(),
               "settings_sha256": sha256(args.settings) if args.settings else None, "staged_state": False,
               "source_git_head_context_only": subprocess.check_output(["git", "-C", str(REPO), "rev-parse", "HEAD"], text=True).strip(),
               "editor_dll_sha256": {file.name: sha256(file) for file in dlls}, "timeout_seconds": timeout,
               "project_commandlet_source_sha256": sha256(project.parent / "Source/WeltallEditor/Private/RenderScreenCommandlet.cpp")}
    (folder / "request.json").write_text(json.dumps(request, indent=2), encoding="utf-8")
    print(json.dumps({"folder": str(folder), "prepared_only": args.prepare_only}), flush=True)
    if args.prepare_only:
        return 0
    start = time.monotonic()
    timed_out = False
    with (folder / "console.txt").open("wb") as console:
        try:
            result = subprocess.run(command, cwd=REPO, stdout=console, stderr=subprocess.STDOUT, timeout=timeout)
            code = result.returncode
        except subprocess.TimeoutExpired:
            timed_out, code = True, None
    summary = summarize_log(folder / "editor.log", folder) if (folder / "editor.log").is_file() else {"missing_log": True}
    summary.update(exit_code=code, timed_out=timed_out, elapsed_wall_seconds=time.monotonic()-start,
                   source_save_unchanged=args.save.read_bytes()==data, isolated_load_slot_unchanged=copied.read_bytes()==data,
                   final_frame_present=(folder / "frame.png").is_file())
    (folder / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in summary.items() if key not in {"transitions", "logged_errors"}}, indent=2))
    return int(code != 0 or not summary["source_save_unchanged"] or not summary["isolated_load_slot_unchanged"]
               or not summary["final_frame_present"] or bool(summary.get("logged_errors")) or bool(summary.get("stopped_program")))


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"render_game_reference: {error}", file=sys.stderr)
        raise SystemExit(1)
