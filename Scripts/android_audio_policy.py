"""Verify actual Android 15+ background audio-focus denial and foreground retry.

Requires an installed Development APK. Restarts only Weltall, preserves normal
save/settings hashes, leaves volume unchanged and retains evidence under out/.
This host-driven test is separate from the self-contained AudioFocus suite.
"""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time
import uuid

from android_game_reference import ACTIVITY, COMMAND_FILE, Device, EXTERNAL, PACKAGE
from android_package import REPO
from android_test import choose_device, default_adb


TEST = "Weltall.Android.HostDrivenAudioPolicy.BackgroundDenialAndPanelRetry"

# These remain in the native report. Accept them only with all actual-policy/PCM assertions passing.
# OpenSL pauses consumption in SuspendContext; its base RequestDeviceSwap is a no-op.
REVIEWED_ENGINE_WARNINGS = (
    r"LogAndroid: SuspendApp_EventThread -> event thread, suspending app, bAppIsActive_EventThread is false so early return!",
    r"LogAudioMixer: AudioMixerPlatformInterface Timeout \[ 5 Seconds\] waiting for h/w\. InstanceID=\d+",
    r"LogVulkanRHI: VkSurfaceCapabilitiesKHR returned \[4,64\] image count, requested 3",
)


def assess_report(value):
    cases = value.get("tests", [])
    warnings = [entry.get("event", {}).get("message", "") for case in cases
                for entry in case.get("entries", []) if entry.get("event", {}).get("type") == "Warning"]
    errors = [entry for case in cases for entry in case.get("entries", [])
              if entry.get("event", {}).get("type") == "Error"]
    expected = all(any(re.fullmatch(pattern, message) for pattern in REVIEWED_ENGINE_WARNINGS) for message in warnings)
    passed = (len(cases) == 1 and cases[0].get("fullTestPath") == TEST and cases[0].get("state") == "Success"
              and value.get("succeeded", 0) + value.get("succeededWithWarnings", 0) == 1
              and all(value.get(key) == 0 for key in ("failed", "notRun", "inProcess"))
              and not errors and expected and (value.get("succeededWithWarnings") == 0 or bool(warnings)))
    return bool(passed), warnings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", type=Path, default=default_adb())
    parser.add_argument("--serial")
    args = parser.parse_args()
    if not args.adb:
        raise ValueError("install platform-tools or specify --adb")
    serial = choose_device(subprocess.check_output([str(args.adb), "devices"], text=True), args.serial)
    run = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    folder = REPO / "out/testing/audio-policy" / run
    folder.mkdir(parents=True, exist_ok=False)
    private = "files/Weltall/Testing/AudioPolicy/" + run
    report_path = EXTERNAL + "/Saved/Automation/" + run
    command = ('-project="../../../Weltall/Weltall.uproject" '
               f'-ExecCmds="Automation RunTests {TEST}" -WeltallAudioPolicy={run} '
               f'-TestExit="Automation Test Queue Empty" -ReportExportPath="{report_path}" -unattended\n').encode()
    summary = {"device": serial, "test": TEST, "run": run, "passed": False, "volume_changed": False}
    before = None
    written = launched = False
    started = time.monotonic()
    print(json.dumps({"folder": str(folder)}), flush=True)
    with (folder / "transport.log").open("w", encoding="utf-8") as log:
        device = Device(args.adb, serial, log)
        try:
            if device.run(["shell", "test", "-e", COMMAND_FILE], check=False).returncode == 0:
                raise RuntimeError("a launch override exists; inspect its active job before testing")
            sdk = int(device.run(["shell", "getprop", "ro.build.version.sdk"]).stdout)
            package_info = device.run(["shell", "dumpsys", "package", PACKAGE]).stdout.decode(errors="replace")
            target = re.search(r"targetSdk=(\d+)", package_info)
            if sdk < 35 or not target or int(target[1]) < 35:
                raise RuntimeError("actual policy-denial test requires Android and target SDK 35 or newer")
            summary.update(sdk=sdk, target_sdk=int(target[1]))
            device.run(["shell", "am", "force-stop", PACKAGE])
            before = device.player_snapshot()
            (folder / "player-before.json").write_text(json.dumps(before, indent=2))
            apk = device.run(["shell", "pm", "path", PACKAGE]).stdout.decode().splitlines()
            summary["installed_apk_hashes"] = []
            for path in apk:
                if not path.startswith("package:/data/app/") or not path.endswith(".apk"):
                    raise RuntimeError("unexpected installed package path")
                summary["installed_apk_hashes"].append(device.run(["shell", "sha256sum " + shlex.quote(path[8:])]).stdout.decode().strip())
            if not apk:
                raise RuntimeError("Development APK is not installed")
            local_command = folder / "UECommandLine.txt"
            local_command.write_bytes(command)
            partial = COMMAND_FILE + "." + run + ".tmp"
            device.run(["shell", "mkdir", "-p", EXTERNAL])
            device.run(["push", str(local_command), partial])
            if device.run(["exec-out", "cat", partial]).stdout != command:
                raise RuntimeError("temporary launch override did not verify")
            device.run(["shell", "test ! -e " + shlex.quote(COMMAND_FILE) + " && mv " + shlex.quote(partial) + " " + shlex.quote(COMMAND_FILE)])
            written = True
            device.run(["shell", "input", "keyevent", "KEYCODE_WAKEUP"])
            device.run(["shell", "wm", "dismiss-keyguard"], check=False)
            launched = True
            device.run(["shell", "am", "start", "-W", "-n", ACTIVITY])
            backgrounded = returned = False
            while time.monotonic() - started < 140:
                ready = device.read(private + "/ready.txt", check=False) if not backgrounded else None
                # exec-out can report exit zero even when remote cat prints a missing-file error.
                if ready is not None and ready.stdout.decode("utf-8-sig", errors="replace").startswith("Press Home;"):
                    summary["pids_before"] = device.run(["shell", "pidof", PACKAGE]).stdout.decode().strip()
                    device.run(["shell", "input", "keyevent", "KEYCODE_HOME"])
                    backgrounded = True
                if backgrounded and not returned:
                    result = device.read(private + "/result.txt", check=False)
                    if re.fullmatch(rb"eligible=(true|false)\nfocused=(true|false)\nimportance=\d+\nstate=-?\d+\nrequest_result=-?\d+\n", result.stdout):
                        (folder / "result.txt").write_bytes(result.stdout)
                        summary["actual_background_denial"] = (b"eligible=true\n" in result.stdout
                            and b"state=5\n" in result.stdout and b"request_result=0\n" in result.stdout)
                        device.run(["shell", "am", "start", "-W", "-n", ACTIVITY])
                        summary["pids_after"] = device.run(["shell", "pidof", PACKAGE]).stdout.decode().strip()
                        returned = True
                report = device.run(["exec-out", "cat", report_path + "/index.json"], check=False)
                if report.returncode == 0:
                    try:
                        value = json.loads(report.stdout.decode("utf-8-sig"))
                    except (ValueError, UnicodeError):
                        time.sleep(0.5)
                        continue
                    (folder / "index.json").write_bytes(report.stdout)
                    cases = value.get("tests", [])
                    summary["counts"] = {key: value.get(key) for key in ("succeeded", "succeededWithWarnings", "failed", "notRun", "inProcess", "totalDuration")}
                    assertions_passed, warnings = assess_report(value)
                    summary["retained_engine_warnings"] = warnings
                    summary["passed_with_reviewed_engine_warnings"] = assertions_passed and bool(warnings)
                    summary["passed"] = (returned and summary.get("actual_background_denial")
                        and summary["pids_before"] == summary["pids_after"] and assertions_passed)
                    break
                time.sleep(0.5)
            else:
                raise RuntimeError("policy test did not produce its report; inspect retained evidence before retrying")
        except Exception as error:
            summary["error"] = str(error)
        finally:
            if launched:
                device.run(["shell", "am", "force-stop", PACKAGE], check=False)
            if written:
                if device.run(["exec-out", "cat", COMMAND_FILE], check=False).stdout == command:
                    device.run(["shell", "rm", "-f", COMMAND_FILE])
                else:
                    summary["cleanup_error"] = "launch override changed; left intact for inspection"
            if before is not None:
                try:
                    after = device.player_snapshot()
                    (folder / "player-after.json").write_text(json.dumps(after, indent=2))
                    summary["player_files_unchanged"] = before == after
                except Exception as error:
                    summary["player_snapshot_error"] = str(error)
            package_uid = re.search(r"uid:(\d+)", device.run(["shell", "pm", "list", "packages", "-U", PACKAGE], check=False).stdout.decode())
            if launched and package_uid:
                (folder / "logcat.txt").write_bytes(device.run(["logcat", "--uid=" + package_uid[1], "-d", "-v", "threadtime"], check=False).stdout)
            summary["elapsed_seconds"] = time.monotonic() - started
            summary["passed"] = bool(summary["passed"] and summary.get("player_files_unchanged") and not summary.get("cleanup_error"))
            (folder / "summary.json").write_text(json.dumps(summary, indent=2))
            print(json.dumps(summary), flush=True)
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
