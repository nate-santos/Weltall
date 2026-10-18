"""Synthetic coverage for the Android native test transport and report handling."""

import hashlib
import io
from pathlib import Path, PurePosixPath
import shlex
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
import android_test as runner


class AndroidRunnerTests(unittest.TestCase):
    def setUp(self):
        temporary_root = runner.REPO / "out" / "python-tests"
        temporary_root.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=temporary_root)
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_device_selection_requires_exactly_one_authorized_phone(self):
        listing = "List of devices attached\nphone1 device product:test\nphone2 unauthorized\n"
        self.assertEqual(runner.choose_device(listing), "phone1")
        with self.assertRaises(ValueError):
            runner.choose_device(listing, "phone2")
        with self.assertRaises(ValueError):
            runner.choose_device(listing + "phone3 device\n")
        self.assertEqual(runner.choose_device(listing + "phone3 device\n", "phone3"), "phone3")

    def test_windows_and_wrong_architecture_binaries_are_rejected(self):
        binary = self.root / "native-test"
        for header in (b"MZ" + bytes(100), b"\x7fELF\x02\x01" + bytes(94)):
            binary.write_bytes(header)
            with self.assertRaisesRegex(ValueError, "ARM ELF"):
                runner.validate_binary(binary)
        header = bytearray(20)
        header[:6] = b"\x7fELF\x02\x01"
        header[18:20] = (183).to_bytes(2, "little")
        binary.write_bytes(header)
        runner.validate_binary(binary)

    def test_junit_results_do_not_hide_skips_or_errors(self):
        report = self.root / "report.xml"
        report.write_text('<testsuites><testsuite><testcase name="pass" time="0.5"/>'
                          '<testcase name="fail" time="1"><failure/></testcase>'
                          '<testcase name="error"><error/></testcase>'
                          '<testcase name="skip"><skipped/></testcase></testsuite></testsuites>')
        counts = runner.report_counts(report)
        self.assertEqual(counts, {"tests": 4, "failures": 1, "errors": 1, "skipped": 1, "seconds": 1.5})
        report.write_text('<testsuites><testsuite/></testsuites>')
        with self.assertRaisesRegex(ValueError, "no test cases"):
            runner.report_counts(report)

    def test_shell_filter_is_a_single_quoted_argument(self):
        remote = PurePosixPath("/data/local/tmp/weltall-tests/Runs/synthetic")
        expression = "A test's name,$(touch /bad); false"
        script = runner.test_script(PurePosixPath("/data/local/tmp/test"), remote, expression)
        command = next(line for line in script.splitlines() if line.startswith("TMPDIR="))
        tokens = shlex.split(command)
        self.assertEqual(tokens[2], expression)
        self.assertIn("/data/local/tmp/weltall-tests/Runs/synthetic/tmp", tokens[0])
        self.assertIn("--reporter", tokens)
        self.assertTrue(any(token.startswith("junit::out=") for token in tokens))
        self.assertTrue(any(token.startswith("xml::out=") for token in tokens))

    def test_native_catch_summary_counts_cases_separately_from_assertions(self):
        report = self.root / "report.catch.xml"
        report.write_text('<Catch2TestRun><OverallResults successes="27" failures="1" expectedFailures="0" skips="1"/>'
                          '<OverallResultsCases successes="2" failures="1" expectedFailures="0" skips="1"/></Catch2TestRun>')
        self.assertEqual(runner.catch_counts(report), {"tests": 4, "passed": 2, "failures": 1,
                         "expected_failures": 0, "skipped": 1, "assertions": 29})

    def test_verified_remote_files_are_reused_without_push(self):
        source = self.root / "synthetic"
        source.write_bytes(b"independently authored synthetic bytes")
        digest = hashlib.sha256(source.read_bytes()).hexdigest()

        class CachedAdb(runner.Adb):
            def __init__(self):
                super().__init__("unused", "phone", io.StringIO())
                self.calls = []

            def run(self, arguments, **kwargs):
                self.calls.append(arguments)
                return subprocess.CompletedProcess(arguments, 0, digest + "\n")

        adb = CachedAdb()
        self.assertEqual(adb.stage(source, PurePosixPath("/data/local/tmp/test"), "run"), digest)
        self.assertFalse(any(call[0] == "push" for call in adb.calls))

    def test_hash_mismatch_never_replaces_final_file(self):
        source = self.root / "synthetic"
        source.write_bytes(b"independently authored synthetic bytes")

        class MismatchAdb(runner.Adb):
            def __init__(self):
                super().__init__("unused", "phone", io.StringIO())
                self.calls = []

            def run(self, arguments, **kwargs):
                self.calls.append(arguments)
                if arguments[0] == "shell" and "sha256sum" in arguments[1]:
                    return subprocess.CompletedProcess(arguments, 0, "0" * 64 + "\n")
                return subprocess.CompletedProcess(arguments, 0, "")

        adb = MismatchAdb()
        with self.assertRaisesRegex(RuntimeError, "not replaced"):
            adb.stage(source, PurePosixPath("/data/local/tmp/test"), "run")
        self.assertTrue(any(call[0] == "push" for call in adb.calls))
        self.assertFalse(any(call[0] == "shell" and call[1].startswith("mv ") for call in adb.calls))


if __name__ == "__main__":
    unittest.main()
