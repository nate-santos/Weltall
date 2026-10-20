"""The interruption driver may signal only its exactly identified standalone writer."""
from pathlib import Path, PurePosixPath
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
import android_save_interruption as runner


class SaveProbeIdentityTests(unittest.TestCase):
    def test_barrier_requires_expected_stage_and_real_pid(self):
        self.assertEqual(runner.parse_barrier("after-close 12345\n", "after-close"), 12345)
        for text in ("after-close 1", "after-close 0", "after-close -9", "before-close 12345", "after-close 12345 extra"):
            with self.assertRaises(ValueError):
                runner.parse_barrier(text, "after-close")

    def test_exact_probe_identity_rejects_game_and_other_runs(self):
        binary = PurePosixPath("/data/local/tmp/test/save-probe")
        root = PurePosixPath("/data/local/tmp/test/weltall-save-probe-after-close-kill")
        parts = [str(binary), "write", str(root), "after-close"]
        self.assertTrue(runner.owns_probe("\0".join(parts) + "\0", binary, root, "after-close"))
        for index, replacement in ((0, "org.weltall.game"), (1, "verify"), (2, str(root) + "-other"), (3, "after-rename")):
            changed = parts.copy()
            changed[index] = replacement
            self.assertFalse(runner.owns_probe("\0".join(changed) + "\0", binary, root, "after-close"))


if __name__ == "__main__":
    unittest.main()
