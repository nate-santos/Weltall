"""Synthetic local APK packaging tests. No original game data is required or read."""

import hashlib
import os
from pathlib import Path, PurePosixPath
import sys
import subprocess
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
import android_package as package


class AndroidPackageTests(unittest.TestCase):
    def setUp(self):
        temporary_root = package.REPO / "out" / "python-tests"
        temporary_root.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=temporary_root)
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def discs(self, nested=False):
        source = self.root / "inputs"
        for number in (1, 2):
            folder = source / (f"disc {number}" if nested else "")
            folder.mkdir(parents=True, exist_ok=True)
            (folder / f"Disc {number}.BIN").write_bytes(bytes([number]) * 2352)
            (folder / f"Disc {number}.cue").write_text(
                f'FILE "Disc {number}.BIN" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n', encoding="utf-8")
        return source

    def test_input_list_contains_only_both_pairs_and_preserves_spaces(self):
        source = self.discs()
        (source / "unrelated.log").write_text("unrelated")
        inputs = package.disc_inputs(source)
        self.assertEqual(len(inputs), 4)
        self.assertEqual([str(path) for path, _ in inputs],
                         ["Disc 1.BIN", "Disc 1.cue", "Disc 2.BIN", "Disc 2.cue"])

    def test_nested_pairs_preserve_cue_relative_paths(self):
        inputs = package.disc_inputs(self.discs(nested=True))
        self.assertEqual(str(inputs[0][0]), "disc 1/Disc 1.BIN")
        self.assertEqual(str(inputs[-1][0]), "disc 2/Disc 2.cue")

    def test_missing_track_fails_before_staging(self):
        source = self.discs()
        (source / "Disc 2.BIN").unlink()
        with self.assertRaises(FileNotFoundError):
            package.disc_inputs(source)

    def test_missing_second_descriptor_is_rejected(self):
        source = self.discs()
        (source / "Disc 2.cue").unlink()
        with self.assertRaisesRegex(ValueError, "exactly two"):
            package.disc_inputs(source)

    def test_descriptor_cannot_escape_input_folder(self):
        source = self.discs()
        (source / "Disc 1.cue").write_text('FILE "../private.bin" BINARY\n')
        with self.assertRaisesRegex(ValueError, "relative"):
            package.disc_inputs(source)

    def test_non_bin_and_non_binary_tracks_are_rejected(self):
        for line in ('FILE "Disc 1.iso" BINARY', 'FILE "Disc 1.BIN" WAVE'):
            source = self.discs()
            (source / "Disc 1.cue").write_text(line)
            with self.assertRaises(ValueError):
                package.disc_inputs(source)

    def test_truncated_raw_sector_is_rejected(self):
        source = self.discs()
        (source / "Disc 1.BIN").write_bytes(b"truncated")
        with self.assertRaisesRegex(ValueError, "2352"):
            package.disc_inputs(source)

    def test_manifest_records_exact_transfer_hash_and_length(self):
        source = self.discs(nested=True)
        inputs = package.disc_inputs(source)
        stage = self.root / "local export" / "Content" / "BundledDiscs"
        total = package.stage_discs(inputs, stage)
        self.assertEqual(total, sum(path.stat().st_size for _, path in inputs))
        lines = (stage / "Manifest.txt").read_text().splitlines()
        self.assertEqual(lines[0], package.MANIFEST_HEADER)
        self.assertEqual(len(lines), 5)
        for line in lines[1:]:
            digest, length, relative = line.split("|")
            data = (stage / relative).read_bytes()
            self.assertEqual(int(length), len(data))
            self.assertEqual(digest, hashlib.sha1(data).hexdigest())
        self.assertFalse((source / "Manifest.txt").exists())

    def test_cue_encoding_and_keywords_are_portable_to_android(self):
        source = self.discs()
        cue = source / "Disc 1.cue"
        cue.write_text('file "Disc 1.BIN" binary\n track 01 mode2/2352\n index 01 00:00:00\n', encoding="utf-8-sig")
        stage = self.root / "stage"
        package.stage_discs(package.disc_inputs(source), stage)
        text = (stage / "Disc 1.cue").read_text(encoding="utf-8")
        self.assertTrue(text.startswith('FILE "Disc 1.BIN" BINARY\n'))
        self.assertIn("TRACK 01 MODE2/2352", text)
        self.assertIn("INDEX 01 00:00:00", text)

    def test_staging_respects_free_space(self):
        inputs = package.disc_inputs(self.discs())
        with mock.patch.object(package.shutil, "disk_usage", return_value=mock.Mock(free=1)):
            with self.assertRaisesRegex(ValueError, "enough space"):
                package.stage_discs(inputs, self.root / "stage")

    def test_invalid_portable_paths_are_rejected(self):
        for name in ("../game.bin", "/root/game.bin", "C:/game.bin", "a//b", "./a", "a|b", "a\nb"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                package.checked_relative(name)
        self.assertEqual(package.checked_relative("dir/game.bin"), PurePosixPath("dir/game.bin"))

    def test_bundle_setting_is_added_only_to_local_export(self):
        project = self.root / "export"
        package.configure_local_discs(project)
        config = (project / "Config/Android/AndroidGame.ini").read_text()
        self.assertIn('+DirectoriesToAlwaysStageAsUFS=(Path="BundledDiscs")', config)

    def test_uat_builds_arm64_android_apk_pipeline(self):
        command = package.uat_command(self.root / "UE", self.root / "project", self.root / "out", "Development")
        for flag in ("BuildCookRun", "-platform=Android", "-cookflavor=ASTC", "-build", "-cook", "-stage", "-pak", "-package"):
            self.assertIn(flag, command)
        self.assertNotIn("-distribution", command)
        self.assertNotIn("-deploy", command)

    def test_no_engine_reports_actionable_blocker(self):
        self.assertIn("UE_ROOT", package.toolchain_problems(None)[0])

    def test_inputs_inside_disposable_export_are_rejected_before_deletion(self):
        source = self.discs()
        with mock.patch.object(package, "PROJECT", source), mock.patch.object(package.shutil, "rmtree") as remove:
            self.assertEqual(package.main(["--include-discs", str(source), "--prepare-only"]), 1)
            remove.assert_not_called()
        self.assertEqual((source / "Disc 1.BIN").stat().st_size, 2352)
        self.assertEqual((source / "Disc 2.BIN").stat().st_size, 2352)

    def test_print_plan_does_not_create_export(self):
        output = self.root / "out" / "android"
        with mock.patch.object(package, "PROJECT", output / "project"), mock.patch.object(package, "OUTPUT", output):
            self.assertEqual(package.main(["--include-discs", str(self.discs()), "--print-plan"]), 0)
        self.assertFalse(output.exists())

    def test_old_apk_cannot_satisfy_a_new_build_without_an_apk(self):
        output = self.root / "out" / "android"
        old = output / "packages" / "Development" / "previous.apk"
        old.parent.mkdir(parents=True)
        old.write_bytes(b"synthetic prior package marker")
        with mock.patch.object(package, "REPO", self.root), mock.patch.object(package, "OUTPUT", output), \
                mock.patch.object(package, "PROJECT", output / "project"), \
                mock.patch.object(package, "setting", return_value=str(self.root / "engine")), \
                mock.patch.object(package, "toolchain_problems", return_value=[]), \
                mock.patch.object(package, "export_revision"), mock.patch.object(package, "run_build", return_value=0):
            self.assertEqual(package.main([]), 1)
        self.assertTrue(old.is_file())

    def test_build_tool_paths_are_child_scoped_and_existing_environment_wins(self):
        (self.root / "local.env").write_text("JAVA_HOME=local java\nANDROID_HOME=local android sdk\n"
                                            "NDKROOT=local ndk\nUNREQUESTED_SECRET=not inherited\n")
        with mock.patch.object(package, "REPO", self.root), \
                mock.patch.dict(package.os.environ, {"JAVA_HOME": "existing java", "UNRELATED_EXISTING": "preserved"}, clear=True), \
                mock.patch.object(package.subprocess, "Popen") as popen:
            child = popen.return_value.__enter__.return_value
            child.stdout = []
            child.wait.return_value = 0
            self.assertEqual(package.run_build(["synthetic-build"], self.root / "build-output.txt"), 0)
            environment = popen.call_args.kwargs["env"]
            self.assertEqual(environment["JAVA_HOME"], "existing java")
            self.assertEqual(environment["ANDROID_HOME"], "local android sdk")
            self.assertEqual(environment["NDKROOT"], "local ndk")
            self.assertEqual(environment["UNRELATED_EXISTING"], "preserved")
            self.assertNotIn("UNREQUESTED_SECRET", environment)
            self.assertNotIn("ANDROID_HOME", package.os.environ)

    def authored_export(self, revision, destination):
        (destination / "Source").mkdir(parents=True)
        (destination / "Source/Keep.cpp").write_text("unchanged authored source\n")
        (destination / "Source/Changed.cpp").write_text("new authored source\n")
        (destination / "Config/Android").mkdir(parents=True)
        (destination / "Config/Android/AndroidGame.ini").write_text("authored packaging settings\n")
        (destination / "Weltall.uproject").write_text("synthetic project descriptor\n")

    def test_refresh_preserves_compilation_cache_and_unchanged_source_timestamps(self):
        project = self.root / "project"
        self.authored_export("old", project)
        kept = project / "Source/Keep.cpp"
        os.utime(kept, ns=(1600000000000000000, 1600000000000000000))
        original_time = kept.stat().st_mtime_ns
        (project / "Source/Changed.cpp").write_text("old authored source\n")
        (project / "Source/Deleted.cpp").write_text("removed authored source\n")
        for directory in package.BUILD_CACHE_DIRS:
            (project / directory).mkdir()
            (project / directory / "cached-object").write_bytes(b"synthetic compilation cache")
        stale = project / "Content/BundledDiscs/old.BIN"
        stale.parent.mkdir(parents=True)
        stale.write_bytes(b"synthetic old staging marker")
        (project / "UnselectedPlugin").mkdir()
        (project / "UnselectedPlugin/extra.txt").write_text("unselected input")
        with mock.patch.object(package, "OUTPUT", self.root / "out"), \
                mock.patch.object(package, "export_revision", side_effect=self.authored_export):
            package.refresh_project("new", project)
        self.assertEqual(kept.stat().st_mtime_ns, original_time)
        self.assertEqual((project / "Source/Changed.cpp").read_text(), "new authored source\n")
        self.assertFalse((project / "Source/Deleted.cpp").exists())
        self.assertFalse((project / "Content").exists())
        self.assertFalse((project / "UnselectedPlugin").exists())
        for directory in package.BUILD_CACHE_DIRS:
            self.assertEqual((project / directory / "cached-object").read_bytes(), b"synthetic compilation cache")

    def test_invalid_revision_leaves_the_existing_project_intact(self):
        project = self.root / "project"
        self.authored_export("old", project)
        with mock.patch.object(package, "OUTPUT", self.root / "out"), \
                mock.patch.object(package, "export_revision", side_effect=subprocess.CalledProcessError(1, "git archive")):
            with self.assertRaises(subprocess.CalledProcessError):
                package.refresh_project("missing", project)
        self.assertEqual((project / "Source/Keep.cpp").read_text(), "unchanged authored source\n")

    def test_next_data_free_export_removes_old_disc_staging_and_clean_is_explicit(self):
        output = self.root / "out/android"
        project = output / "project"
        source = self.discs()
        with mock.patch.object(package, "REPO", self.root), mock.patch.object(package, "OUTPUT", output), \
                mock.patch.object(package, "PROJECT", project), \
                mock.patch.object(package, "export_revision", side_effect=self.authored_export):
            self.assertEqual(package.main(["--include-discs", str(source), "--prepare-only"]), 0)
            self.assertTrue((project / "Content/BundledDiscs/Manifest.txt").is_file())
            cached = project / "Intermediate/compiled-engine-object"
            cached.parent.mkdir()
            cached.write_bytes(b"synthetic engine compilation cache")
            self.assertEqual(package.main(["--prepare-only"]), 0)
            self.assertTrue(cached.is_file())
            self.assertFalse((project / "Content/BundledDiscs").exists())
            self.assertNotIn("BundledDiscs", (project / "Config/Android/AndroidGame.ini").read_text())
            self.assertEqual(package.main(["--prepare-only", "--clean-export"]), 0)
            self.assertFalse(cached.exists())


if __name__ == "__main__":
    unittest.main()
