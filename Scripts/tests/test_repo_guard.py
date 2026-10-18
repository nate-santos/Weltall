"""Synthetic-only coverage for the index, commit and outgoing-history guards."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


GUARD = Path(__file__).resolve().parents[1] / "repo_guard.py"
SPEC = importlib.util.spec_from_file_location("repo_guard", GUARD)
assert SPEC and SPEC.loader
guard = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = guard
SPEC.loader.exec_module(guard)


class ContentTests(unittest.TestCase):
    def test_paths_are_case_insensitive_and_nested(self):
        for path in (
            "ROM.BIN", "local/DiSc.CuE", "Source/ASSETS/named.txt",
            "nested/Output/result.txt", "Output/raw", "nested/TESTING/frame.png",
            "captures/frame.png", "one/Build/two.txt", "out/anything.cpp",
            "Source/release.APK", "Tests/Data/archive.7z", "Source/state.MCD",
            "native/.cxx/metadata.json", "nested/build-arm64/cache.txt",
        ):
            with self.subTest(path=path):
                self.assertIsNotNone(guard.path_problem(path))

    def test_source_and_synthetic_test_code_are_allowed(self):
        for path in (
            "Tests/WeltallCore/Chd/ChdFixtures.cpp", "Scripts/tests/test_repo_guard.py",
            "Source/Weltall/Android/WeltallControls.cpp", "Docs/AndroidPortPlan.md",
            ".github/workflows/repository-data-guard.yml", "Content/Story/custom.txt",
            "Tests/Support/SoundTestFiles.cpp", "Source/Weltall/Weltall.Build.cs",
        ):
            with self.subTest(path=path):
                self.assertIsNone(guard.path_problem(path))

    def test_game_signatures_are_detected_under_renamed_files(self):
        samples = (
            b"MComprHD" + bytes(100), b"PS-X EXE" + bytes(100),
            b"\x00PBP" + bytes(100), b"ECM\x00" + bytes(100),
            guard.CD_SYNC + bytes(2352 - len(guard.CD_SYNC)),
            b'FILE "synthetic.data" BINARY\r\n  TRACK 01 MODE2/2352\r\n',
            b'file "synthetic.data" binary\n track 01 mode1/2048\n',
        )
        for sample in samples:
            with self.subTest(signature=sample[:10]):
                self.assertIsNotNone(guard.content_problem(sample, len(sample)))

    def test_iso_descriptor_in_each_sector_format_is_detected(self):
        for sector_size, offset in ((2048, 0), (2352, 16), (2352, 24)):
            sample = bytearray(guard.INSPECT_BYTES)
            start = 16 * sector_size + offset
            sample[start:start + 7] = b"\x01CD001\x01"
            with self.subTest(sector_size=sector_size, offset=offset):
                self.assertIsNotNone(guard.content_problem(bytes(sample), len(sample)))

    def test_packaged_data_and_large_blobs_are_detected(self):
        for sample in (b"PK\x03\x04", b"\x7fELF", b"\x1f\x8b", b"7z\xbc\xaf\x27\x1c"):
            self.assertIsNotNone(guard.content_problem(sample, len(sample)))
        self.assertIsNotNone(guard.content_problem(b"", guard.MAX_BLOB_BYTES + 1))
        self.assertIsNotNone(guard.content_problem(b"MC", 128 * 1024))
        self.assertIsNotNone(guard.content_problem(b"version https://git-lfs.github.com/spec/v1\n", 150))

    def test_source_text_can_discuss_formats_and_create_synthetic_headers(self):
        text = b'// Generate a synthetic CHD: const char *header = "MComprHD";\n'
        self.assertIsNone(guard.content_problem(text, len(text)))


class RepositoryTests(unittest.TestCase):
    def setUp(self):
        # Keep child Git processes inside the same writable workspace on Windows.
        # The sandbox's redirected system temp may not grant them directory access.
        test_root = GUARD.parent.parent / "out" / "repo-guard-tests"
        test_root.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="synthetic-", dir=test_root)
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name)
        self.git("init", "-q")
        self.git("config", "user.name", "Guard Test")
        self.git("config", "user.email", "guard-test@example.invalid")
        self.git("config", "commit.gpgsign", "false")
        self.git("config", "core.autocrlf", "false")
        self.write("Source/test.cpp", b"// synthetic test source\n")
        self.git("add", ".")
        self.commit("safe baseline")
        self.base = self.git("rev-parse", "HEAD").strip().decode("ascii")

    def git(self, *args: str) -> bytes:
        result = subprocess.run(["git", "-C", str(self.repo), *args], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", "replace"))
        return result.stdout

    def commit(self, message: str) -> None:
        self.git("-c", "core.hooksPath=", "commit", "-q", "--no-verify", "-m", message)

    def write(self, path: str, data: bytes) -> None:
        destination = self.repo / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)

    def run_guard(self, *args: str, stdin: str | None = None) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(GUARD), "--repo", str(self.repo), *args],
            input=stdin, text=True, capture_output=True,
        )

    def test_safe_index_and_tracked_head_pass(self):
        self.assertEqual(self.run_guard("--staged").returncode, 0)
        self.assertEqual(self.run_guard("--tracked").returncode, 0)

    def test_forced_ignored_rom_is_rejected_without_reading_worktree(self):
        self.write(".gitignore", b"*.BIN\n")
        self.write("local/disc.BIN", b"synthetic, not a game image")
        self.git("add", ".gitignore")
        self.git("add", "-f", "local/disc.BIN")
        (self.repo / "local/disc.BIN").unlink()
        result = self.run_guard("--staged")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("disc.BIN", result.stderr)

    def test_staged_renamed_disc_is_rejected_even_when_worktree_is_safe(self):
        self.write("Source/renamed.dat", b"PS-X EXE" + bytes(80))
        self.git("add", "Source/renamed.dat")
        self.write("Source/renamed.dat", b"safe unstaged replacement")
        result = self.run_guard("--staged")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("PlayStation executable", result.stderr)

    def test_staged_safe_source_passes_even_when_worktree_has_local_data(self):
        self.write("Source/renamed.dat", b"staged synthetic text")
        self.git("add", "Source/renamed.dat")
        self.write("Source/renamed.dat", b"PS-X EXE" + bytes(80))
        self.assertEqual(self.run_guard("--staged").returncode, 0)

    def test_tracked_tree_does_not_use_index(self):
        self.write("local/renamed.dat", b"MComprHD" + bytes(64))
        self.git("add", "local/renamed.dat")
        self.assertEqual(self.run_guard("--tracked").returncode, 0)
        self.assertEqual(self.run_guard("--staged").returncode, 1)

    def test_committed_blob_is_rejected_after_it_is_deleted_locally(self):
        self.write("Source/renamed.dat", b"MComprHD" + bytes(64))
        self.git("add", "Source/renamed.dat")
        self.commit("unsafe synthetic header")
        (self.repo / "Source/renamed.dat").unlink()
        self.assertEqual(self.run_guard("--tracked").returncode, 1)

    def test_large_blob_is_rejected_without_content_read(self):
        self.write("Source/oversized.dat", bytes(guard.MAX_BLOB_BYTES + 1))
        self.git("add", "Source/oversized.dat")
        # Unit-test the reader too: no process is started, so asking Git for
        # this oversized object's content would fail this test immediately.
        reader = guard.BlobReader(self.repo)
        self.assertIsNotNone(reader.inspect("unused", guard.MAX_BLOB_BYTES + 1))
        result = self.run_guard("--staged")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("8 MiB", result.stderr)

    def test_pre_push_catches_bad_intermediate_commit_removed_from_tip(self):
        self.write("local/renamed.dat", b"PS-X EXE" + bytes(80))
        self.git("add", "local/renamed.dat")
        self.commit("unsafe synthetic intermediate")
        self.git("rm", "-q", "local/renamed.dat")
        self.commit("remove synthetic intermediate")
        head = self.git("rev-parse", "HEAD").strip().decode("ascii")
        self.assertEqual(self.run_guard("--tracked").returncode, 0)
        update = f"refs/heads/android-port {head} refs/heads/android-port {self.base}\n"
        result = self.run_guard("--pre-push", "origin", stdin=update)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("PlayStation executable", result.stderr)
        result = self.run_guard("--commit-range", self.base + ".." + head)
        self.assertEqual(result.returncode, 1, result.stderr)

    def test_new_remote_branch_is_checked(self):
        self.git("update-ref", "refs/remotes/origin/main", self.base)
        self.write("Source/renamed.dat", guard.CD_SYNC + bytes(100))
        self.git("add", "Source/renamed.dat")
        self.commit("synthetic raw header")
        head = self.git("rev-parse", "HEAD").strip().decode("ascii")
        update = f"refs/heads/android-port {head} refs/heads/android-port {'0' * 40}\n"
        result = self.run_guard("--pre-push", "origin", stdin=update)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("raw CD sector", result.stderr)

    def test_safe_outgoing_update_and_remote_deletion_pass(self):
        self.write("Source/new.cpp", b"// more synthetic source\n")
        self.git("add", "Source/new.cpp")
        self.commit("safe source")
        head = self.git("rev-parse", "HEAD").strip().decode("ascii")
        update = f"refs/heads/android-port {head} refs/heads/android-port {self.base}\n"
        self.assertEqual(self.run_guard("--pre-push", "origin", stdin=update).returncode, 0)
        deletion = f"(delete) {'0' * 40} refs/heads/old {head}\n"
        self.assertEqual(self.run_guard("--pre-push", "origin", stdin=deletion).returncode, 0)


if __name__ == "__main__":
    unittest.main()
