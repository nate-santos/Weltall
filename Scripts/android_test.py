"""Run the native ARM64 Catch2 tests on an explicitly connected Android device.

    python Scripts/android_test.py --preset touch
    python Scripts/android_test.py --preset synthetic
    python Scripts/android_test.py --stage-only --with-discs ../bin --story Content/Story
    python Scripts/android_test.py --preset disc-smoke --with-discs ../bin
    python Scripts/android_test.py --preset media-smoke --with-discs ../bin

The default executable is out/tests/android-arm64/WeltallCoreTests. Build it with
WELTALL_GAMES_DIR=/data/local/tmp/weltall-tests/Games and
WELTALL_STORY_DIR=/data/local/tmp/weltall-tests/Story. The executable must be a
64-bit little-endian ARM ELF binary, normally linked with ANDROID_STL=c++_static.

The default synthetic preset excludes all [Content] tests and uploads no game
data. Supplying --with-discs explicitly permits uploading the two local CUE/BIN
pairs to the phone's separate test directory. Files are streamed by adb, verified
with SHA-256 and replaced atomically. No APK/app data is changed. Results and any
normalized CUE descriptors remain only in ignored out/testing/android/.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import subprocess
import sys
import time
import uuid
import xml.etree.ElementTree as ET

from android_package import checked_relative, disc_inputs, portable_cue


REPO = Path(__file__).resolve().parent.parent
REMOTE = PurePosixPath("/data/local/tmp/weltall-tests")
LOCAL_RESULTS = REPO / "out" / "testing" / "android"
PRESETS = {
    "synthetic": "~[Content]",
    "touch": "[Touch]~[Content]",
    "disc-smoke": "Disc 1 is identified and holds only its boot files,Disc 2 is identified and holds only its boot files",
    "media-smoke": "Disc 1*,Disc 2*,A movie streams whole frames and all of its sound",
    "content": "[Content]",
}
DISC_PRESETS = {"disc-smoke", "media-smoke", "content"}


def device_list(text):
    devices = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[1] in {"device", "unauthorized", "offline", "recovery", "sideload"}:
            devices[fields[0]] = fields[1]
    return devices


def choose_device(text, serial=None):
    devices = device_list(text)
    if serial:
        if devices.get(serial) != "device":
            raise ValueError("the selected Android device is unavailable or has not authorized USB debugging")
        return serial
    ready = [name for name, state in devices.items() if state == "device"]
    if len(ready) != 1:
        raise ValueError("connect and authorize one Android phone, or select one with --serial")
    return ready[0]


def validate_binary(binary):
    with Path(binary).open("rb") as source:
        header = source.read(20)
    if (len(header) < 20 or header[:4] != b"\x7fELF" or header[4:6] != b"\x02\x01"
            or int.from_bytes(header[18:20], "little") != 183):
        raise ValueError("--binary must be a 64-bit little-endian ARM ELF executable (not a Windows build)")


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        while block := source.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


class Adb:
    def __init__(self, executable, serial, log, transfer_timeout=600):
        self.executable = str(executable)
        self.serial = serial
        self.log = log
        self.transfer_timeout = transfer_timeout

    def run(self, arguments, timeout=60, check=True):
        command = [self.executable]
        if self.serial:
            command.extend(["-s", self.serial])
        command.extend(str(argument) for argument in arguments)
        self.log.write("> " + subprocess.list2cmdline(command) + "\n")
        self.log.flush()
        kwargs = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, encoding="utf-8", errors="replace", timeout=timeout, **kwargs)
        self.log.write(result.stdout + ("" if result.stdout.endswith("\n") else "\n"))
        self.log.flush()
        if check and result.returncode:
            raise RuntimeError(f"adb {arguments[0]} failed ({result.returncode}); see the local transport log")
        return result

    def shell(self, script, **kwargs):
        return self.run(["shell", script], **kwargs)

    def hash(self, remote):
        result = self.shell("toybox sha256sum -b " + shlex.quote(str(remote)), check=False,
                            timeout=self.transfer_timeout)
        value = result.stdout.strip().splitlines()
        return value[-1].strip().lower() if result.returncode == 0 and value and re.fullmatch(r"[0-9a-fA-F]{64}", value[-1].strip()) else None

    def stage(self, source, remote, run_id):
        """Never publish a partly uploaded executable, descriptor or image."""
        remote = PurePosixPath(remote)
        digest = sha256_file(source)
        if self.hash(remote) == digest:
            print(f"Reusing verified device file: {remote.name}", flush=True)
            return digest
        self.shell("mkdir -p " + shlex.quote(str(remote.parent)))
        partial = str(remote) + ".partial-" + run_id
        print(f"Uploading and verifying: {remote.name}", flush=True)
        self.run(["push", str(Path(source).resolve()), partial], timeout=self.transfer_timeout)
        if self.hash(partial) != digest:
            raise RuntimeError(f"device upload verification failed for {remote.name}; the final file was not replaced")
        self.shell("mv -f " + shlex.quote(partial) + " " + shlex.quote(str(remote)))
        return digest


def stage_discs(adb, folder, result_dir, run_id):
    inputs = disc_inputs(folder)
    expected = set()
    records = []
    for relative, source in inputs:
        remote = REMOTE / "Games" / str(relative)
        expected.add(str(remote))
        upload = source
        if source.suffix.lower() == ".cue":
            upload = result_dir / "staging" / Path(*relative.parts)
            upload.parent.mkdir(parents=True, exist_ok=True)
            upload.write_text(portable_cue(source), encoding="utf-8", newline="\n")
        digest = adb.stage(upload, remote, run_id)
        records.append({"relative": str(relative), "bytes": upload.stat().st_size, "sha256": digest})
    # Refuse a mixed test library instead of silently selecting an older duplicate disc.
    listing = adb.shell("find " + shlex.quote(str(REMOTE / "Games")) + " -type f").stdout
    unexpected = [name for name in listing.splitlines() if PurePosixPath(name).suffix.lower() in {".bin", ".cue", ".chd", ".iso", ".img"} and name not in expected]
    if unexpected:
        raise ValueError("the device test Games directory has additional disc images; move those aside before testing this pair")
    return records


def stage_story(adb, folder, run_id):
    source = Path(folder).resolve(strict=True)
    files = sorted(path for path in source.glob("*/*.txt") if path.is_file())
    if not files:
        raise ValueError("--story must contain authored story packs with .txt documents")
    records = []
    expected = set()
    for path in files:
        path.resolve(strict=True).relative_to(source)
        relative = checked_relative(path.relative_to(source).as_posix())
        remote = REMOTE / "Story" / str(relative)
        expected.add(str(remote))
        digest = adb.stage(path, remote, run_id)
        records.append({"relative": str(relative), "bytes": path.stat().st_size, "sha256": digest})
    listing = adb.shell("find " + shlex.quote(str(REMOTE / "Story")) + " -type f").stdout
    if any(name for name in listing.splitlines() if PurePosixPath(name).suffix.lower() == ".txt" and name not in expected):
        raise ValueError("the device test Story directory has additional documents; move those aside before comparing this source revision")
    return records


def test_script(binary, remote_run, test_filter, no_discs=False):
    """Quote every caller-controlled shell value, including Catch2 filter metacharacters."""
    quote = lambda value: shlex.quote(str(value))
    command = " ".join(quote(value) for value in (
        binary, test_filter, "--reporter", "junit::out=" + str(remote_run / "report.junit.xml"),
        "--reporter", "xml::out=" + str(remote_run / "report.catch.xml"), "--durations", "yes"))
    return "\n".join([
        "cd " + quote(remote_run) + " || exit 125",
        "mkdir -p " + quote(remote_run / "tmp") + " || exit 125",
        "TMPDIR=" + quote(remote_run / "tmp") + (" WELTALL_TEST_NO_DISCS=1" if no_discs else "")
        + " " + command + " >" + quote(remote_run / "console.txt") + " 2>&1 &",
        "test_pid=$!",
        'printf "%s\\n" "$test_pid" >' + quote(remote_run / "pid.txt"),
        'wait "$test_pid"',
        "test_status=$?",
        'printf "%s\\n" "$test_status" >' + quote(remote_run / "exit-status.txt"),
        'exit "$test_status"',
    ])


def stop_test(adb, binary, remote_run):
    # Only stop the recorded process if it still names this exact managed test executable.
    quote = lambda value: shlex.quote(str(value))
    script = "\n".join([
        "if [ -r " + quote(remote_run / "pid.txt") + " ]; then",
        "  read test_pid <" + quote(remote_run / "pid.txt"),
        '  case "$test_pid" in ""|*[!0-9]*) exit 0;; esac',
        '  test_exe=$(readlink "/proc/$test_pid/exe")',
        '  if [ "$test_exe" = ' + quote(binary) + ' ]; then kill "$test_pid"; fi',
        "fi",
    ])
    adb.shell(script, check=False)


def report_counts(path):
    root = ET.parse(path).getroot()
    cases = list(root.iter("testcase"))
    if not cases:
        raise ValueError("the Catch2 report contains no test cases; check the selected filter")
    return {
        "tests": len(cases),
        "failures": sum(case.find("failure") is not None for case in cases),
        "errors": sum(case.find("error") is not None for case in cases),
        "skipped": sum(case.find("skipped") is not None or case.get("status") in {"notrun", "skipped"} for case in cases),
        "seconds": sum(float(case.get("time", "0")) for case in cases),
    }


def catch_counts(path):
    """Catch's own summary counts test cases; JUnit counts can include nested sections."""
    root = ET.parse(path).getroot()
    cases = root.find("OverallResultsCases")
    assertions = root.find("OverallResults")
    if cases is None or assertions is None:
        raise ValueError("the native Catch2 XML report has no completed run summary")
    values = {name: int(cases.get(name, "0")) for name in ("successes", "failures", "expectedFailures", "skips")}
    total = sum(values.values())
    if total == 0:
        raise ValueError("the native Catch2 run selected no test cases")
    return {"tests": total, "passed": values["successes"], "failures": values["failures"],
            "expected_failures": values["expectedFailures"], "skipped": values["skips"],
            "assertions": sum(int(assertions.get(name, "0")) for name in ("successes", "failures", "expectedFailures"))}


def default_adb():
    local = REPO / "out" / "tools" / "platform-tools" / ("adb.exe" if os.name == "nt" else "adb")
    return str(local) if local.is_file() else shutil.which("adb")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", type=Path, default=REPO / "out/tests/android-arm64/WeltallCoreTests")
    parser.add_argument("--adb", default=default_adb())
    parser.add_argument("--serial", help="explicit adb device serial (required when several are connected)")
    parser.add_argument("--preset", choices=PRESETS, default="synthetic")
    parser.add_argument("--filter", dest="test_filter", help="Catch2 test expression overriding the preset")
    parser.add_argument("--with-discs", type=Path, help="explicitly stage the two supplied CUE/BIN pairs to the phone")
    parser.add_argument("--story", type=Path, help="stage authored Content/Story documents to the phone")
    parser.add_argument("--stage-only", action="store_true", help="stage optional inputs without needing a test executable")
    parser.add_argument("--timeout", type=int, default=600, help="maximum test execution seconds (default: 600)")
    parser.add_argument("--transfer-timeout", type=int, default=600, help="maximum seconds per adb transfer/hash")
    parser.add_argument("--allow-skips", action="store_true", help="accept skipped cases (for example optional undub fixtures)")
    args = parser.parse_args(argv)
    if not args.adb:
        parser.error("adb was not found; use the existing platform-tools executable with --adb")
    if args.timeout <= 0 or args.transfer_timeout <= 0:
        parser.error("timeouts must be positive")
    if not args.stage_only:
        try:
            validate_binary(args.binary)
        except (OSError, ValueError) as error:
            parser.error(str(error))
    if not args.test_filter and args.preset in DISC_PRESETS and not args.with_discs:
        parser.error("disc-dependent presets require --with-discs; verified files are reused when already present")
    if args.with_discs:
        try:
            disc_inputs(args.with_discs)  # Validate all relationships before any device mutation.
        except (OSError, ValueError) as error:
            parser.error(str(error))
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    result_dir = LOCAL_RESULTS / run_id
    result_dir.mkdir(parents=True, exist_ok=False)
    remote_run = REMOTE / "Runs" / run_id
    summary = {"run_id": run_id, "filter": args.test_filter or PRESETS[args.preset], "results": str(result_dir)}
    try:
        with (result_dir / "transport.log").open("w", encoding="utf-8", buffering=1) as log:
            adb = Adb(args.adb, args.serial, log, args.transfer_timeout)
            adb.serial = choose_device(adb.run(["devices", "-l"]).stdout, args.serial)
            summary["device"] = adb.serial
            summary["model"] = adb.shell("getprop ro.product.model").stdout.strip()
            abi = adb.shell("getprop ro.product.cpu.abi").stdout.strip()
            if abi != "arm64-v8a":
                raise ValueError("the selected phone is not an ARM64 Android target")
            adb.shell("mkdir -p " + shlex.quote(str(remote_run)))
            if args.with_discs:
                summary["disc_inputs"] = stage_discs(adb, args.with_discs, result_dir, run_id)
            if args.story:
                summary["story_inputs"] = stage_story(adb, args.story, run_id)
            if args.stage_only:
                summary["staged_only"] = True
                summary["success"] = True
                print(f"Device inputs staged and verified. Local record: {result_dir}", flush=True)
                return 0
            remote_binary = REMOTE / "Binaries" / ("WeltallCoreTests-" + sha256_file(args.binary)[:16])
            summary["binary_sha256"] = adb.stage(args.binary, remote_binary, run_id)
            adb.shell("chmod 700 " + shlex.quote(str(remote_binary)))
            print(f"Running {summary['filter']} on {summary['model']}. Local results: {result_dir}", flush=True)
            timed_out = False
            started = time.monotonic()
            try:
                no_discs = not args.with_discs or (not args.test_filter and args.preset in {"synthetic", "touch"})
                summary["disc_access_disabled"] = no_discs
                completed = adb.shell(test_script(remote_binary, remote_run, summary["filter"], no_discs=no_discs), timeout=args.timeout, check=False)
                status = completed.returncode
            except subprocess.TimeoutExpired:
                timed_out = True
                status = 124
                stop_test(adb, remote_binary, remote_run)
            finally:
                for filename in ("report.junit.xml", "report.catch.xml", "console.txt", "exit-status.txt"):
                    adb.run(["pull", str(remote_run / filename), str(result_dir / filename)], check=False)
            summary["exit_code"] = status
            summary["elapsed_seconds"] = time.monotonic() - started
            summary["timed_out"] = timed_out
            report = result_dir / "report.junit.xml"
            native_report = result_dir / "report.catch.xml"
            if report.is_file() and native_report.is_file() and not timed_out:
                summary.update(report_counts(report))
                summary["junit_entries"] = summary["tests"]
                summary["junit_section_seconds"] = summary.pop("seconds")
                summary.update(catch_counts(native_report))
            else:
                raise RuntimeError("test execution timed out or did not produce a complete JUnit report; inspect local logs")
            summary["success"] = status == 0 and summary["failures"] == 0 and summary["errors"] == 0 and (args.allow_skips or summary["skipped"] == 0)
            print(f"{summary['tests']} test cases, {summary['failures']} failed, {summary['errors']} errors, {summary['skipped']} skipped.", flush=True)
            print(f"Local report: {report}", flush=True)
            return 0 if summary["success"] else 1
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired, ET.ParseError) as error:
        summary["success"] = False
        summary["error"] = str(error)
        print(f"android_test: {error}\nLocal results: {result_dir}", file=sys.stderr, flush=True)
        return 1
    finally:
        (result_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
