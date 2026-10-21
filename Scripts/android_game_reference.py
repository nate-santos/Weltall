"""Run a private scripted gameplay comparison in an installed Development APK.

Uses the same normal-load/controller script as render_game_reference.py. Restarts
only org.weltall.game, uses a new app-private test save store, preserves normal
saves/settings, never changes volume, and removes its temporary launch override.
All inputs/captures/reports remain in ignored out/. No gameplay acceptance is
granted automatically. The app is left stopped after the run; reopen it normally.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
import math
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tarfile
import time
import uuid

from android_package import REPO, checked_relative
from android_test import choose_device, default_adb
from render_game_reference import pad_script, summarize_log


PACKAGE = "org.weltall.game"
ACTIVITY = PACKAGE + "/com.epicgames.unreal.GameActivity"
EXTERNAL = "/sdcard/Android/data/" + PACKAGE + "/files/UnrealGame/Weltall"
COMMAND_FILE = EXTERNAL + "/UECommandLine.txt"
TEST = "Weltall.Android.GameplayReference"


def request_text(seconds, every, reader=False, route=None, route_at=36):
    if not 40 <= seconds <= 7200 or not math.isfinite(every) or not 1 <= every <= 60:
        raise ValueError("use 40–7,200 seconds and a capture interval from 1 to 60 seconds")
    if not math.isfinite(route_at) or not 36 <= route_at < seconds:
        raise ValueError("route start must be at/after 36 seconds and before the reference ends")
    text = f"-Seconds={seconds} -Every={every}"
    if reader:
        text += " -Reader=0.5,0.02"
    if route:
        if not re.fullmatch(r"!?-?\d+,-?\d+(;!?-?\d+,-?\d+)*", route):
            raise ValueError("route must contain integer x,z or !x,z points joined by ';'")
        text += f" -Route={route} -RouteAt={route_at}"
    return text + "\n"


def report_passed(report):
    cases = report.get("tests", [])
    return (report.get("succeeded") == 1 and all(report.get(key) == 0 for key in
            ("failed", "notRun", "inProcess", "succeededWithWarnings")) and len(cases) == 1
            and cases[0].get("fullTestPath") == TEST and cases[0].get("state") == "Success")


def unpack_capture(data, destination):
    """Only regular files within this unique test directory, never archive links."""
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for member in archive:
            if member.isdir():
                continue
            if not member.isfile():
                raise ValueError("device capture archive contains a non-regular file")
            name = member.name.removeprefix("./")
            relative = checked_relative(name)
            target = destination.joinpath(*relative.parts)
            if member.size > 64 * 1024 * 1024 or not target.resolve().is_relative_to(destination.resolve()):
                raise ValueError("device capture file is too large or outside its test directory")
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(member) as source, target.open("wb") as output:
                while chunk := source.read(1024 * 1024):
                    output.write(chunk)


class Device:
    def __init__(self, adb, serial, log):
        self.prefix = [str(adb), "-s", serial]
        self.log = log

    def run(self, args, *, data=None, check=True, timeout=60):
        command = self.prefix + args
        self.log.write(subprocess.list2cmdline(command) + "\n")
        self.log.flush()
        result = subprocess.run(command, input=data, capture_output=True, timeout=timeout)
        if check and result.returncode:
            self.log.write(result.stderr.decode(errors="replace") + "\n")
            raise RuntimeError(f"device command failed ({result.returncode}); see transport.log")
        return result

    def app(self, script, **kwargs):
        return self.run(["exec-out", "run-as", PACKAGE, "sh", "-c", script], **kwargs)

    def read(self, path, **kwargs):
        return self.app("cat " + shlex.quote(path), **kwargs)

    def stage(self, path, data):
        # exec-in both closes stdin at EOF and selects binary mode on Windows.
        # exec-out is read-only; shell stdin can treat 0x1a as end-of-file there.
        self.run(["exec-in", "run-as", PACKAGE, "sh", "-c", "cat >" + shlex.quote(path)], data=data)
        if self.read(path).stdout != data:
            raise RuntimeError("the private reference input did not verify after upload")

    def player_snapshot(self):
        # Includes every normal save, LastSaved, preferences and any pending temporaries.
        text = self.app("find files/Weltall/Saves files/Weltall/Settings -type f -exec sha256sum {} \\;").stdout.decode()
        lines = sorted(line for line in text.splitlines() if line.strip())
        if not lines or any(not re.match(r"^[0-9a-f]{64}  files/Weltall/(Saves|Settings)/", line) for line in lines):
            raise RuntimeError("could not fingerprint the normal player saves and settings")
        return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--save", type=Path, required=True, help="checksum-valid 7,936-byte native save, read only")
    parser.add_argument("--actions", type=Path, help="same timed JSON controller actions as the PC runner")
    parser.add_argument("--load-disc", type=int, choices=(1, 2), default=1)
    parser.add_argument("--seconds", type=int, default=70)
    parser.add_argument("--every", type=float, default=20)
    parser.add_argument("--reader", action="store_true")
    parser.add_argument("--route")
    parser.add_argument("--route-at", type=float, default=36)
    parser.add_argument("--serial")
    parser.add_argument("--adb", default=default_adb())
    parser.add_argument("--label", default="reference")
    parser.add_argument("--timeout", type=float)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", args.label):
        raise ValueError("use a short letters/digits/dashes label")
    params = request_text(args.seconds, args.every, args.reader, args.route, args.route_at)
    data = args.save.read_bytes()
    if len(data) != 7936 or sum(data[:-1]) % 256 != data[-1]:
        raise ValueError("the source must be a checksum-valid 7,936-byte native save")
    actions = json.loads(args.actions.read_text(encoding="utf-8-sig")) if args.actions else []
    controller = pad_script(args.seconds, actions, args.load_disc).encode()
    timeout = args.timeout if args.timeout is not None else args.seconds * 3 + 180
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("timeout must be finite and positive")
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8] + "-" + args.label
    folder = REPO / "out/testing/android-game-reference" / run_id
    folder.mkdir(parents=True, exist_ok=False)
    for name, content in (("input.sav", data), ("controller.txt", controller), ("request.txt", params.encode())):
        (folder/name).write_bytes(content)
    metadata = {"source_save": str(args.save.resolve()), "source_save_sha256": hashlib.sha256(data).hexdigest(),
                "actions": actions, "request": params, "staged_state": False, "timeout_seconds": timeout,
                "source_git_head_context_only": subprocess.check_output(["git", "-C", str(REPO), "rev-parse", "HEAD"], text=True).strip()}
    (folder/"request.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(json.dumps({"folder": str(folder), "prepared_only": args.prepare_only}), flush=True)
    if args.prepare_only:
        return 0
    if not args.adb:
        raise ValueError("install Android platform-tools or specify --adb")
    serial = choose_device(subprocess.check_output([str(args.adb), "devices"], text=True), args.serial)
    root = "files/Weltall/Testing/GameplayReference/" + run_id
    remote_report = EXTERNAL + "/Saved/Automation/" + run_id
    command = ('-project="../../../Weltall/Weltall.uproject" '
               f'-ExecCmds="Automation RunTests {TEST}" -WeltallReference={run_id} '
               f'-TestExit="Automation Test Queue Empty" -ReportExportPath="{remote_report}" -unattended\n').encode()
    summary = {"device": serial, "gameplay_acceptance": "not assessed", "visual_review_required": True}
    before = None
    written = launched = False
    started = time.monotonic()
    with (folder/"transport.log").open("w", encoding="utf-8") as log:
        device = Device(args.adb, serial, log)
        try:
            if device.run(["shell", "test", "-e", COMMAND_FILE], check=False).returncode == 0:
                raise RuntimeError("a launch override already exists; inspect it before running a reference")
            device.run(["shell", "am", "force-stop", PACKAGE])
            before = device.player_snapshot()
            (folder/"player-before.json").write_text(json.dumps(before, indent=2), encoding="utf-8")
            settings = device.read("files/Weltall/Settings/Settings.txt").stdout
            (folder/"player-settings.txt").write_bytes(settings)
            metadata["player_settings_sha256"] = hashlib.sha256(settings).hexdigest()
            apk_paths = device.run(["shell", "pm", "path", PACKAGE]).stdout.decode().splitlines()
            metadata["installed_apks_sha256"] = []
            for line in apk_paths:
                if not line.startswith("package:/data/app/") or not line.endswith(".apk"):
                    raise RuntimeError("unexpected installed package path")
                digest = device.run(["shell", "sha256sum " + shlex.quote(line.removeprefix("package:"))]).stdout.decode().strip()
                if not re.match(r"^[0-9a-f]{64}  ", digest):
                    raise RuntimeError("could not fingerprint the installed APK")
                metadata["installed_apks_sha256"].append(digest)
            if not apk_paths:
                raise RuntimeError("the Development APK is not installed")
            (folder/"request.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
            device.app("mkdir -p files/Weltall/Testing/GameplayReference && mkdir " + shlex.quote(root))
            for name in ("input.sav", "controller.txt", "request.txt"):
                device.stage(root + "/" + name, (folder/name).read_bytes())
            device.run(["shell", "mkdir", "-p", EXTERNAL])
            local_command = folder/"UECommandLine.txt"
            local_command.write_bytes(command)
            partial_command = COMMAND_FILE + "." + run_id + ".tmp"
            device.run(["push", str(local_command), partial_command])
            if device.run(["exec-out", "cat", partial_command]).stdout != command:
                raise RuntimeError("the temporary launch override failed upload verification")
            written = True
            device.run(["shell", "test ! -e " + shlex.quote(COMMAND_FILE) + " && mv "
                        + shlex.quote(partial_command) + " " + shlex.quote(COMMAND_FILE)])
            device.run(["shell", "input", "keyevent", "KEYCODE_WAKEUP"])
            device.run(["shell", "wm", "dismiss-keyguard"], check=False)
            launched = True
            launch = device.run(["shell", "am", "start", "-W", "-n", ACTIVITY])
            (folder/"launch.txt").write_bytes(launch.stdout + launch.stderr)
            progress_at = 0.0
            while time.monotonic() - started < timeout:
                result = device.run(["exec-out", "cat", remote_report + "/index.json"], check=False)
                if result.returncode == 0:
                    try:
                        report = json.loads(result.stdout.decode("utf-8-sig"))
                    except (ValueError, UnicodeError):
                        pass
                    else:
                        (folder/"index.json").write_bytes(result.stdout)
                        summary["automation_passed"] = report_passed(report)
                        summary["automation_counts"] = {key: report.get(key) for key in
                            ("succeeded", "succeededWithWarnings", "failed", "notRun", "totalDuration")}
                        break
                if time.monotonic() - progress_at >= 30:
                    progress = device.read(root + "/progress.json", check=False)
                    if progress.returncode == 0:
                        try:
                            value = json.loads(progress.stdout)
                        except ValueError:
                            pass
                        else:
                            print(json.dumps({"progress": value}), flush=True)
                    progress_at = time.monotonic()
                time.sleep(2)
            else:
                raise RuntimeError("no completed automation report before timeout; inspect this run before retrying")
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            summary["error"] = str(error)
        finally:
            # No scripted input can continue after this host stops monitoring it.
            if launched:
                device.run(["shell", "am", "force-stop", PACKAGE], check=False)
            if "partial_command" in locals():
                device.run(["shell", "rm", "-f", partial_command], check=False)
            if written:
                current = device.run(["exec-out", "cat", COMMAND_FILE], check=False)
                if current.returncode == 0 and current.stdout == command:
                    device.run(["shell", "rm", "-f", COMMAND_FILE])
                else:
                    summary["launch_override_changed_or_missing"] = True
            summary["launch_override_absent"] = device.run(["shell", "test", "-e", COMMAND_FILE], check=False).returncode != 0
            if before is not None:
                after = device.player_snapshot()
                (folder/"player-after.json").write_text(json.dumps(after, indent=2), encoding="utf-8")
                summary["normal_player_files_unchanged"] = before == after
                capture = device.app("tar -cf - -C " + shlex.quote(root) + " .", check=False, timeout=180)
                if capture.returncode == 0:
                    unpack_capture(capture.stdout, folder/"device")
            summary["source_save_unchanged"] = args.save.read_bytes() == data
            summary["elapsed_wall_seconds"] = time.monotonic() - started
            trace = folder/"device/trace.log"
            if trace.is_file():
                summary.update(summarize_log(trace, folder/"device"))
            summary["final_frame_present"] = (folder/"device/frame.png").is_file()
            (folder/"summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    passed = (not summary.get("error") and summary.get("automation_passed") and summary.get("normal_player_files_unchanged")
              and summary["source_save_unchanged"] and summary["launch_override_absent"] and summary["final_frame_present"]
              and not summary.get("stopped_program") and not summary.get("logged_errors"))
    print(json.dumps({key: value for key, value in summary.items() if key not in {"transitions", "logged_errors"}}, indent=2), flush=True)
    return int(not passed)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"android_game_reference: {error}", file=sys.stderr)
        raise SystemExit(1)
