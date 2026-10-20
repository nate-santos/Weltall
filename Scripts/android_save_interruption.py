"""Kill/suspend a standalone ARM64 save writer at verified libc boundaries.

Build WeltallSaveInterruptionProbe first, then run this script with one authorized
Android phone connected. Only generated test saves under /data/local/tmp are used.
The game APK, game process, player saves and ROM files are not accessed.
"""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess
import time
import uuid

from android_test import Adb, REPO, choose_device, default_adb, sha256_file, validate_binary


PHASES = ("before-write", "after-write", "before-close", "after-close", "before-rename",
          "after-rename", "before-metadata-rename", "after-metadata-rename")


def parse_barrier(text, expected):
    fields = text.split()
    if len(fields) != 2 or fields[0] != expected or not re.fullmatch(r"[0-9]+", fields[1]) or int(fields[1]) <= 1:
        raise ValueError("unexpected save-writer barrier marker")
    return int(fields[1])


def owns_probe(command_line, binary, root, phase):
    return command_line.rstrip("\0\r\n").split("\0") == [str(binary), "write", str(root), phase]


def signal_probe(adb, pid, binary, root, phase, signal):
    if signal not in {"KILL", "CONT"}:
        raise ValueError("unsupported test-writer signal")
    command_line = adb.shell(f"cat /proc/{pid}/cmdline", check=False)
    if command_line.returncode or not owns_probe(command_line.stdout, binary, root, phase):
        raise RuntimeError("refusing to signal a PID that is not this exact test writer")
    adb.shell(f"kill -{signal} {pid}")


def snapshot(adb, root):
    files = {}
    for name in ("Card1/No01.sav", "Card8/No15.sav", "LastSaved.txt", "Card8/No15.sav.new", "LastSaved.txt.new"):
        path = root / name
        exists = adb.shell("test -f " + shlex.quote(str(path)), check=False).returncode == 0
        files[name] = {"sha256": adb.hash(path), "bytes": int(adb.shell("wc -c < " + shlex.quote(str(path))).stdout)} if exists else None
    return files


def run_case(adb, binary, root, phase, action, directory):
    quote = lambda path: shlex.quote(str(path))
    adb.shell(f"{quote(binary)} prepare {quote(root)}")
    before = snapshot(adb, root)
    process, pid = None, None
    started = time.monotonic()
    output_path = directory / f"{phase}-{action}.txt"
    try:
        command = [adb.executable, "-s", adb.serial, "shell", f"exec {quote(binary)} write {quote(root)} {phase}"]
        with output_path.open("wb") as output:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
            while time.monotonic() - started < 15:
                marker = adb.shell("cat " + quote(root / "barrier.txt"), check=False)
                if marker.returncode == 0:
                    pid = parse_barrier(marker.stdout, phase)
                    status = adb.shell(f"cat /proc/{pid}/status").stdout
                    if re.search(r"^State:\s+T", status, re.MULTILINE):
                        break
                if process.poll() is not None:
                    raise RuntimeError("writer ended without reaching a verified stopped boundary")
                time.sleep(.05)
            else:
                raise RuntimeError("writer did not stop at its boundary within 15 seconds")
            stopped = snapshot(adb, root)
            # Match command line immediately before any signal; never signal the game or a reused PID.
            if action == "kill":
                signal_probe(adb, pid, binary, root, phase, "KILL")
                exit_code = process.wait(timeout=10)
                if exit_code != 137:
                    raise RuntimeError(f"killed writer returned {exit_code}, expected SIGKILL status 137")
                new_save = phase in {"after-rename", "before-metadata-rename", "after-metadata-rename"}
                mark = 0x33 if new_save else 0x22
                last = 119 if phase == "after-metadata-rename" else 0
            else:
                time.sleep(1)
                signal_probe(adb, pid, binary, root, phase, "CONT")
                exit_code = process.wait(timeout=10)
                if exit_code != 0:
                    raise RuntimeError(f"resumed writer returned {exit_code}")
                mark, last = 0x33, 119
            adb.shell(f"{quote(binary)} verify {quote(root)} {mark} {last}")
            after = snapshot(adb, root)
            # Retry through the same production API, proving abandoned .new files do not poison the next save.
            adb.shell(f"{quote(binary)} write {quote(root)} none")
            adb.shell(f"{quote(binary)} verify {quote(root)} 51 119")
            retried = snapshot(adb, root)
            if retried["Card8/No15.sav.new"] or retried["LastSaved.txt.new"]:
                raise RuntimeError("completed retry left an unfinished temporary file")
            return {"phase": phase, "action": action, "pid": pid, "verified_stopped": True,
                    "exit_code": exit_code, "expected_mark": mark, "expected_last_slot": last,
                    "before": before, "stopped": stopped, "after": after, "retried": retried,
                    "elapsed_seconds": time.monotonic() - started, "passed": True}
    finally:
        if process is not None and process.poll() is None:
            try:
                if pid is not None:
                    signal_probe(adb, pid, binary, root, phase, "KILL")
                process.wait(timeout=5)
            except (RuntimeError, subprocess.TimeoutExpired):
                process.kill()  # Only this local adb child; remote signalling still requires exact identity.
                process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=REPO / "out/tests/android-arm64/WeltallSaveInterruptionProbe")
    parser.add_argument("--adb", default=default_adb())
    parser.add_argument("--serial")
    args = parser.parse_args()
    try:
        validate_binary(args.binary)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    directory = REPO / "out/testing/save-interruption" / run_id
    directory.mkdir(parents=True, exist_ok=False)
    remote = PurePosixPath("/data/local/tmp/weltall-tests/SaveInterruption") / run_id
    binary = remote / "save-probe"
    summary = {"binary_sha256": sha256_file(args.binary), "cases": [], "results": str(directory),
               "scope": "Production FSaveStore, generated files, standalone Android process; not a game Activity or power-loss test"}
    sources = ("Source/WeltallCore/Private/Game/SaveStore.cpp", "Source/WeltallCore/Public/Game/SaveStore.h",
               "Source/WeltallCore/Private/Game/OriginalSave.cpp", "Source/WeltallCore/Public/Game/OriginalSave.h",
               "Tests/Tools/SaveInterruptionProbe.cpp")
    summary["source_sha256"] = {name: sha256_file(REPO / name) for name in sources}
    try:
        with (directory / "transport.log").open("w", encoding="utf-8", buffering=1) as log:
            adb = Adb(args.adb, args.serial, log)
            adb.serial = choose_device(adb.run(["devices", "-l"]).stdout, args.serial)
            summary["device"] = adb.serial
            summary["model"] = adb.shell("getprop ro.product.model").stdout.strip()
            adb.shell("mkdir -p " + shlex.quote(str(remote)))
            adb.stage(args.binary, binary, run_id)
            adb.shell("chmod 700 " + shlex.quote(str(binary)))
            for phase in PHASES:
                for action in ("kill", "resume"):
                    root = remote / f"weltall-save-probe-{phase}-{action}"
                    result = run_case(adb, binary, root, phase, action, directory)
                    summary["cases"].append(result)
                    print(f"{phase}/{action}: passed", flush=True)
        summary["passed"] = len(summary["cases"]) == len(PHASES) * 2
        return 0 if summary["passed"] else 1
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        summary.update(passed=False, error=str(error))
        print(str(error), flush=True)
        return 1
    finally:
        (directory / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
        print(f"Results: {directory}", flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
