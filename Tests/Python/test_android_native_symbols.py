"""Exercise stripping qualification with a synthetic ARM64 shared library."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Scripts'))
from android_native_symbols import Elf, compare


class NativeSymbolsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.repo = Path(__file__).resolve().parents[2]
        cls.tools = cls.repo / 'out/tools/android-ndk-r27c/toolchains/llvm/prebuilt/windows-x86_64/bin'
        if not (cls.tools / 'clang.exe').is_file():
            raise unittest.SkipTest('matching Android NDK not installed')

    def setUp(self):
        root = self.repo / 'out/python-tests'
        root.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=root)
        self.root = Path(self.temporary.name)
        source = self.root / 'fixture.c'
        source.write_text('int native_fixture(int x) { return x * 7 + 3; }\n')
        self.original = self.root / 'original.so'
        subprocess.run([str(self.tools/'clang.exe'), '--target=aarch64-linux-android31', '-shared',
                        '-nostdlib', '-fuse-ld=lld', '-g', '-Wl,--build-id=sha1,-z,max-page-size=16384',
                        str(source), '-o', str(self.original)], check=True, capture_output=True,
                       env={**os.environ, 'TEMP': str(self.root), 'TMP': str(self.root)})
        self.stripped = self.root / 'stripped.so'
        shutil.copyfile(self.original, self.stripped)
        subprocess.run([str(self.tools/'llvm-strip.exe'), '--strip-unneeded', str(self.stripped)], check=True)

    def tearDown(self):
        self.temporary.cleanup()

    def test_stripped_binary_preserves_runtime_and_build_id(self):
        result = compare(self.original, self.stripped)
        self.assertTrue(result['runtime_sections_identical'])
        self.assertLess(result['packaged_bytes'], result['original_bytes'])
        self.assertGreater(Elf(self.original).function_address(), 0)

    def test_unstripped_package_fails(self):
        with self.assertRaisesRegex(ValueError, 'unstripped'):
            compare(self.original, self.original)

    def test_changed_executable_bytes_fail(self):
        section = Elf(self.stripped).sections['.text']
        with self.stripped.open('r+b') as stream:
            stream.seek(section[4])
            value = stream.read(1)[0]
            stream.seek(section[4])
            stream.write(bytes([value ^ 1]))
        with self.assertRaisesRegex(ValueError, 'runtime sections'):
            compare(self.original, self.stripped)


if __name__ == '__main__':
    unittest.main()
