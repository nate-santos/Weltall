"""Generated transport/report fixtures only; no device, engine or game data."""

import io
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
import android_game_reference as reference


class AndroidGameReferenceTests(unittest.TestCase):
    def test_binary_upload_uses_exec_in_and_rejects_truncated_readback(self):
        device = reference.Device("unused", "unused", io.StringIO())
        data = bytes(range(256)) * 32  # Includes Windows stdin's special 0x1a byte.
        calls = []
        device.run = lambda args, **kwargs: calls.append((args, kwargs))
        device.read = lambda path: SimpleNamespace(stdout=data)
        device.stage("files/Weltall/Testing/generated.bin", data)
        self.assertEqual(calls[0][0][0], "exec-in")
        self.assertEqual(calls[0][1]["data"], data)
        device.read = lambda path: SimpleNamespace(stdout=data[:26])
        with self.assertRaises(RuntimeError):
            device.stage("files/Weltall/Testing/generated.bin", data)

    def test_request_allows_normal_routes_and_rejects_other_command_line_options(self):
        text = reference.request_text(70, 20, True, "0,0;!-480,-480", 40)
        self.assertIn("-Reader=0.5,0.02", text)
        self.assertIn("-Route=0,0;!-480,-480 -RouteAt=40", text)
        for route in ("0,0 -Staging=1", "../Saves", "0,0\n-Quit", "nan,2"):
            with self.subTest(route=route), self.assertRaises(ValueError):
                reference.request_text(70, 20, route=route)
        self.assertEqual(reference.request_text(70, 0), "-Seconds=70 -Every=0\n")
        for seconds, every, at in ((39, 20, 36), (7201, 20, 36), (70, float("nan"), 36), (70, -1, 36), (70, 0.5, 36), (70, 20, 70)):
            with self.assertRaises(ValueError):
                reference.request_text(seconds, every, route_at=at)

    def test_only_the_exact_completed_warning_free_test_can_pass(self):
        report = {"succeeded": 1, "failed": 0, "notRun": 0, "inProcess": 0, "succeededWithWarnings": 0,
                  "tests": [{"fullTestPath": reference.TEST, "state": "Success"}]}
        self.assertTrue(reference.report_passed(report))
        for update in ({"tests": []}, {"inProcess": 1}, {"notRun": 1}, {"succeededWithWarnings": 1},
                       {"tests": [{"fullTestPath": "Different.Test", "state": "Success"}]},
                       {"tests": [{"fullTestPath": reference.TEST, "state": "Fail"}]}):
            with self.subTest(update=update):
                self.assertFalse(reference.report_passed(report | update))

    def test_capture_archive_cannot_escape_output_or_create_links(self):
        root = reference.REPO / "out/python-tests"
        root.mkdir(parents=True, exist_ok=True)
        for name, link in (("../outside.txt", False), ("/outside.txt", False), ("link", True)):
            with self.subTest(name=name), tempfile.TemporaryDirectory(dir=root) as temporary:
                data = io.BytesIO()
                with tarfile.open(fileobj=data, mode="w") as archive:
                    entry = tarfile.TarInfo(name)
                    if link:
                        entry.type = tarfile.SYMTYPE
                        entry.linkname = "../outside.txt"
                    archive.addfile(entry)
                with self.assertRaises(ValueError):
                    reference.unpack_capture(data.getvalue(), Path(temporary))

    def test_regular_nested_capture_is_retained_exactly(self):
        root = reference.REPO / "out/python-tests"
        root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as temporary:
            data = io.BytesIO()
            with tarfile.open(fileobj=data, mode="w") as archive:
                entry = tarfile.TarInfo("./nested/generated.txt")
                entry.size = 4
                archive.addfile(entry, io.BytesIO(b"test"))
            reference.unpack_capture(data.getvalue(), Path(temporary))
            self.assertEqual((Path(temporary)/"nested/generated.txt").read_bytes(), b"test")


if __name__ == "__main__":
    unittest.main()
