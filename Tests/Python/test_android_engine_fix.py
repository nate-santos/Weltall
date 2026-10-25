"""Verify the explicit engine edit is bounded, idempotent and non-destructive."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
import android_engine_fix as fix
import android_package as package


class EngineFixTests(unittest.TestCase):
    def setUp(self):
        temp_root = Path(__file__).resolve().parents[2] / "out/python-tests"
        temp_root.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=temp_root)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        version = self.root / "Engine/Build/Build.version"
        version.parent.mkdir(parents=True)
        version.write_text(json.dumps({"MajorVersion": 5, "MinorVersion": 8}))
        self.source = self.root / fix.SOURCE
        self.source.parent.mkdir(parents=True)
        self.data = b"// fixture\r\n\t" + fix.OLD + b"\r\n// end\r\n"
        self.source.write_bytes(self.data)
        header = self.root / fix.HEADER
        header.parent.mkdir(parents=True)
        header.write_bytes(b"GetApproximateCurrentTime()")

    def test_explicit_apply_preserves_surroundings_and_is_idempotent(self):
        self.assertTrue(fix.needs_volume_time_fix(self.root))
        self.assertTrue(any("volume callback" in problem for problem in package.toolchain_problems(self.root)))
        self.assertIn("required", fix.fix_volume_time(self.root))
        self.assertEqual(self.source.read_bytes(), self.data)
        self.assertIn("rebuild", fix.fix_volume_time(self.root, apply=True))
        self.assertEqual(self.source.read_bytes(), self.data.replace(fix.OLD, fix.NEW))
        self.assertFalse(fix.needs_volume_time_fix(self.root))
        self.assertFalse(any("volume callback" in problem for problem in package.toolchain_problems(self.root)))
        self.assertEqual(fix.fix_volume_time(self.root, apply=True), "already corrected")

    def test_unknown_or_multiple_callbacks_are_not_modified(self):
        for content in (b"unrecognized source", self.data + self.data):
            self.source.write_bytes(content)
            with self.assertRaises(ValueError):
                fix.fix_volume_time(self.root, apply=True)
            self.assertEqual(self.source.read_bytes(), content)

    def test_wrong_engine_or_missing_api_is_not_modified(self):
        version = self.root / "Engine/Build/Build.version"
        version.write_text(json.dumps({"MajorVersion": 5, "MinorVersion": 7}))
        with self.assertRaises(ValueError):
            fix.fix_volume_time(self.root, apply=True)
        version.write_text(json.dumps({"MajorVersion": 5, "MinorVersion": 8}))
        (self.root / fix.HEADER).write_bytes(b"unrecognized API")
        with self.assertRaises(ValueError):
            fix.fix_volume_time(self.root, apply=True)
        self.assertEqual(self.source.read_bytes(), self.data)

    def test_exec_guard_preserves_handler_and_newlines_and_is_idempotent(self):
        source = self.root / fix.EXEC_SOURCE
        source.parent.mkdir(parents=True)
        for newline in (b"\n", b"\r\n"):
            data = newline.join((b"// before", fix.EXEC_START, b"{ /* existing handler */",
                                 fix.EXEC_END, b"// after", b""))
            source.write_bytes(data)
            self.assertTrue(fix.needs_exec_commands_fix(self.root))
            self.assertTrue(any("embedded exec" in item for item in package.toolchain_problems(self.root)))
            self.assertIn("required", fix.fix_exec_commands(self.root))
            self.assertEqual(source.read_bytes(), data)
            fix.fix_exec_commands(self.root, apply=True)
            expected = data.replace(fix.EXEC_START, fix.EXEC_GUARD + newline + fix.EXEC_START)
            expected = expected.replace(fix.EXEC_END, fix.EXEC_END + newline + b"#endif // UE_ALLOW_EXEC_COMMANDS")
            self.assertEqual(source.read_bytes(), expected)
            self.assertFalse(fix.needs_exec_commands_fix(self.root))
            self.assertEqual(fix.fix_exec_commands(self.root, apply=True), "already corrected")
            self.assertEqual(source.read_bytes(), expected)

    def test_exec_guard_rejects_unknown_or_partial_handler(self):
        source = self.root / fix.EXEC_SOURCE
        source.parent.mkdir(parents=True)
        for data in (b"unknown", fix.EXEC_START, fix.EXEC_START * 2 + fix.EXEC_END,
                     fix.EXEC_END + fix.EXEC_START,
                     fix.EXEC_GUARD + b"\n" + fix.EXEC_START + fix.EXEC_END):
            source.write_bytes(data)
            with self.assertRaises(ValueError):
                fix.fix_exec_commands(self.root, apply=True)
            self.assertEqual(source.read_bytes(), data)


if __name__ == "__main__":
    unittest.main()
