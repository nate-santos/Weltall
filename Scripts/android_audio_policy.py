"""Verify actual Android 15+ background audio-focus denial and foreground retry.

Requires an installed Development APK. Restarts only Weltall, preserves normal
save/settings hashes, leaves volume unchanged and retains evidence under out/.
This host-driven test is separate from the self-contained AudioFocus suite.
"""

import argparse
import hashlib
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time
import uuid

from android_game_reference import ACTIVITY, COMMAND_FILE, Device, EXTERNAL, PACKAGE
from android_package import REPO, build_environment, setting
from android_test import choose_device, default_adb


TEST = "Weltall.Android.HostDrivenAudioPolicy.BackgroundDenialAndPanelRetry"
DELAYED_TEST = "Weltall.Android.HostDrivenAudioPolicy.LockedDelayedGrant"

# These remain in the native report. Accept them only with all actual-policy/PCM assertions passing.
# OpenSL pauses consumption in SuspendContext; its base RequestDeviceSwap is a no-op.
REVIEWED_ENGINE_WARNINGS = (
    r"LogAndroid: SuspendApp_EventThread -> event thread, suspending app, bAppIsActive_EventThread is false so early return!",
    r"LogAudioMixer: AudioMixerPlatformInterface Timeout \[ 5 Seconds\] waiting for h/w\. InstanceID=\d+",
    r"LogVulkanRHI: VkSurfaceCapabilitiesKHR returned \[4,64\] image count, requested 3",
)


def assess_report(value, expected_test=TEST):
    cases = value.get("tests", [])
    warnings = [entry.get("event", {}).get("message", "") for case in cases
                for entry in case.get("entries", []) if entry.get("event", {}).get("type") == "Warning"]
    errors = [entry for case in cases for entry in case.get("entries", [])
              if entry.get("event", {}).get("type") == "Error"]
    expected = all(any(re.fullmatch(pattern, message) for pattern in REVIEWED_ENGINE_WARNINGS) for message in warnings)
    passed = (len(cases) == 1 and cases[0].get("fullTestPath") == expected_test and cases[0].get("state") == "Success"
              and value.get("succeeded", 0) + value.get("succeededWithWarnings", 0) == 1
              and all(value.get(key) == 0 for key in ("failed", "notRun", "inProcess"))
              and not errors and expected and (value.get("succeededWithWarnings") == 0 or bool(warnings)))
    return bool(passed), warnings


def build_focus_lock(folder):
    sdk, java = setting("ANDROID_HOME") or setting("ANDROID_SDK_ROOT"), setting("JAVA_HOME")
    if not sdk or not java:
        raise ValueError("delayed-focus tests require SDK/JDK paths in local.env or the environment")
    sdk, java = Path(sdk), Path(java)
    source = REPO / "Scripts/Android/WeltallAudioFocusLock.java"
    classes, dex = folder / "classes", folder / "dex"
    classes.mkdir(); dex.mkdir()
    commands = [[str(java / ("bin/javac.exe" if os.name == "nt" else "bin/javac")), "--release", "8", "-classpath", str(sdk / "platforms/android-36/android.jar"),
                 "-d", str(classes), str(source)],
                [str(sdk / ("build-tools/36.0.0/d8.bat" if os.name == "nt" else "build-tools/36.0.0/d8")), "--min-api", "31", "--lib", str(sdk / "platforms/android-36/android.jar"),
                 "--output", str(dex)]]
    with (folder / "focus-lock-build.txt").open("wb") as log:
        for index, command in enumerate(commands):
            if index == 1:
                command += [str(path) for path in sorted(classes.glob("*.class"))]
            result = subprocess.run(command, env=build_environment(), capture_output=True, timeout=60)
            log.write(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError("focus-lock helper did not compile; inspect focus-lock-build.txt")
    binary = dex / "classes.dex"
    (folder / "focus-lock-build-source.json").write_text(json.dumps({
        "source": str(source), "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "dex_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "minimum_api": 31,
        "compile_api": 36, "build_tools": "36.0.0", "jdk": str(java)}, indent=2), encoding="utf-8")
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", type=Path, default=default_adb())
    parser.add_argument("--serial")
    parser.add_argument("--case", choices=("denied", "delayed"), default="denied")
    args = parser.parse_args()
    delayed = args.case == "delayed"
    test = DELAYED_TEST if delayed else TEST
    if not args.adb:
        raise ValueError("install platform-tools or specify --adb")
    serial = choose_device(subprocess.check_output([str(args.adb), "devices"], text=True), args.serial)
    run = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    folder = REPO / "out/testing/audio-policy" / run
    folder.mkdir(parents=True, exist_ok=False)
    private = "files/Weltall/Testing/AudioPolicy/" + run
    report_path = EXTERNAL + "/Saved/Automation/" + run
    command = ('-project="../../../Weltall/Weltall.uproject" '
               f'-ExecCmds="Automation RunTests {test}" -WeltallAudioPolicy={run} '
               f'-TestExit="Automation Test Queue Empty" -ReportExportPath="{report_path}" -unattended\n').encode()
    summary = {"device": serial, "test": test, "run": run, "passed": False, "volume_changed": False}
    before = None
    written = launched = False
    helper = helper_log = None
    shell_root = "/data/local/tmp/weltall-tests/AudioPolicy/" + run
    started = time.monotonic()
    print(json.dumps({"folder": str(folder)}), flush=True)
    with (folder / "transport.log").open("w", encoding="utf-8") as log:
        device = Device(args.adb, serial, log)
        try:
            if device.run(["shell", "test", "-e", COMMAND_FILE], check=False).returncode == 0:
                raise RuntimeError("a launch override exists; inspect its active job before testing")
            dex = build_focus_lock(folder) if delayed else None
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
            policy_observed = helper_released = False
            background_at = 0.0
            while time.monotonic() - started < 140:
                ready = device.read(private + "/ready.txt", check=False) if not backgrounded else None
                # exec-out can report exit zero even when remote cat prints a missing-file error.
                if ready is not None and ready.stdout.decode("utf-8-sig", errors="replace").startswith("Press Home;"):
                    if delayed:
                        if helper is None:
                            device.run(["shell", "test ! -e " + shlex.quote(shell_root) + " && mkdir -p " + shlex.quote(shell_root)])
                            device.run(["push", str(dex), shell_root + "/classes.dex"])
                            digest = device.run(["shell", "sha256sum", shell_root + "/classes.dex"]).stdout.decode().split()
                            if not digest or digest[0] != hashlib.sha256(dex.read_bytes()).hexdigest():
                                raise RuntimeError("focus-lock helper did not verify after upload")
                            helper_log = (folder / "focus-lock-device.txt").open("wb")
                            helper = subprocess.Popen(device.prefix + ["shell", "CLASSPATH=" + shlex.quote(shell_root + "/classes.dex")
                                + " app_process /system/bin WeltallAudioFocusLock " + shlex.quote(shell_root)], stdout=helper_log, stderr=subprocess.STDOUT)
                        locked = device.run(["exec-out", "cat", shell_root + "/locked.txt"], check=False).stdout
                        if not re.fullmatch(rb"uid=2000\nrequest_result=1\npid=\d+\n", locked):
                            if helper.poll() is not None:
                                raise RuntimeError("focus-lock helper ended before obtaining its own lock")
                            time.sleep(0.2)
                            continue
                        (folder / "focus-lock-owner.txt").write_bytes(locked)
                    summary["pids_before"] = device.run(["shell", "pidof", PACKAGE]).stdout.decode().strip()
                    device.run(["shell", "input", "keyevent", "KEYCODE_HOME"])
                    backgrounded = True
                    background_at = time.monotonic()
                if delayed and backgrounded and not returned and time.monotonic() - background_at >= 2.0:
                    device.run(["shell", "am", "start", "-W", "-n", ACTIVITY])
                    summary["pids_after"] = device.run(["shell", "pidof", PACKAGE]).stdout.decode().strip()
                    returned = True
                if backgrounded and not returned:
                    result = device.read(private + "/result.txt", check=False)
                    if re.fullmatch(rb"eligible=(true|false)\nfocused=(true|false)\nimportance=\d+\nstate=-?\d+\nrequest_result=-?\d+\n", result.stdout):
                        (folder / "result.txt").write_bytes(result.stdout)
                        summary["actual_background_denial"] = (b"eligible=true\n" in result.stdout
                            and b"state=5\n" in result.stdout and b"request_result=0\n" in result.stdout)
                        device.run(["shell", "am", "start", "-W", "-n", ACTIVITY])
                        summary["pids_after"] = device.run(["shell", "pidof", PACKAGE]).stdout.decode().strip()
                        returned = True
                        policy_observed = summary["actual_background_denial"]
                if delayed and returned:
                    result = device.read(private + "/result.txt", check=False).stdout
                    if re.fullmatch(rb"eligible=true\nfocused=true\nimportance=\d+\nstate=4\nrequest_result=2\n", result):
                        (folder / "result.txt").write_bytes(result)
                        summary["actual_delayed_grant"] = policy_observed = True
                    release = device.read(private + "/release-ready.txt", check=False).stdout
                    if not helper_released and release.decode("utf-8-sig", errors="replace").startswith("Release own focus lock."):
                        device.run(["shell", "touch", shell_root + "/release"])
                        helper_released = True
                report = device.run(["exec-out", "cat", report_path + "/index.json"], check=False)
                if report.returncode == 0:
                    try:
                        value = json.loads(report.stdout.decode("utf-8-sig"))
                    except (ValueError, UnicodeError):
                        time.sleep(0.5)
                        continue
                    (folder / "index.json").write_bytes(report.stdout)
                    summary["counts"] = {key: value.get(key) for key in ("succeeded", "succeededWithWarnings", "failed", "notRun", "inProcess", "totalDuration")}
                    assertions_passed, warnings = assess_report(value, test)
                    summary["retained_engine_warnings"] = warnings
                    summary["passed_with_reviewed_engine_warnings"] = assertions_passed and bool(warnings)
                    summary["passed"] = (returned and policy_observed
                        and summary["pids_before"] == summary["pids_after"] and assertions_passed)
                    break
                time.sleep(0.5)
            else:
                raise RuntimeError("policy test did not produce its report; inspect retained evidence before retrying")
        except Exception as error:
            summary["error"] = str(error)
        finally:
            if helper is not None:
                device.run(["shell", "touch", shell_root + "/release"], check=False)
                try:
                    summary["focus_lock_helper_exit"] = helper.wait(timeout=25)
                except subprocess.TimeoutExpired:
                    summary["cleanup_error"] = "focus-lock helper did not exit after its bounded deadline; inspect it"
                helper_log.close()
                text = (folder / "focus-lock-device.txt").read_text(errors="replace")
                summary["own_focus_lock_released"] = "own_focus_abandon=1" in text and "own_policy_unregistered=true" in text
                summary["passed"] = bool(summary["passed"] and summary.get("focus_lock_helper_exit") == 0 and summary["own_focus_lock_released"])
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
