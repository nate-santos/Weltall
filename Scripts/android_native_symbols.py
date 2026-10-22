"""Verify stripped APK libraries against their private, matching symbol archives."""

import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile


class Elf:
    def __init__(self, path):
        self.path = Path(path)
        with self.path.open('rb') as source:
            header = source.read(64)
            if len(header) != 64 or header[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', header, 18)[0] != 183:
                raise ValueError(f'not an ARM64 little-endian ELF: {path}')
            offset = struct.unpack_from('<Q', header, 40)[0]
            size, count, names_index = struct.unpack_from('<HHH', header, 58)
            if size != 64 or not count or names_index >= count:
                raise ValueError('unsupported ELF section table')
            source.seek(offset)
            sections = [struct.unpack('<IIQQQQIIQQ', source.read(size)) for _ in range(count)]
            names = sections[names_index]
            source.seek(names[4])
            strings = source.read(names[5])
            self.sections = {}
            for section in sections[1:]:
                name = strings[section[0]:].split(b'\0', 1)[0].decode('ascii')
                self.sections[name] = section
            phoff = struct.unpack_from('<Q', header, 32)[0]
            phsize, phcount = struct.unpack_from('<HH', header, 54)
            source.seek(phoff)
            self.loads = []
            for _ in range(phcount):
                segment = struct.unpack('<IIQQQQQQ', source.read(phsize))
                if segment[0] == 1:
                    _, flags, offset, address, _, files, memory, alignment = segment
                    if alignment < 16384 or alignment & (alignment - 1) or files > memory or any(
                            offset % page != address % page for page in (4096, 16384)):
                        raise ValueError('invalid 4K/16K ELF load alignment')
                    self.loads.append((flags, address, files, memory, alignment))
            if not self.loads:
                raise ValueError('ELF has no load segments')

    def digest(self, section):
        digest = hashlib.sha256()
        if section[1] == 8:  # SHT_NOBITS: no bytes in the file.
            return None
        with self.path.open('rb') as source:
            source.seek(section[4])
            left = section[5]
            while left:
                block = source.read(min(left, 1024 * 1024))
                if not block:
                    raise ValueError('truncated ELF section')
                digest.update(block)
                left -= len(block)
        return digest.hexdigest()

    def runtime(self):
        return {name: (section[1], section[2], section[3], section[5], section[8], self.digest(section))
                for name, section in self.sections.items() if section[2] & 2}

    def build_id(self):
        section = self.sections.get('.note.gnu.build-id')
        if not section:
            raise ValueError('missing native build ID')
        with self.path.open('rb') as source:
            source.seek(section[4])
            names, length, kind = struct.unpack('<III', source.read(12))
            if kind != 3 or source.read(names) != b'GNU\0':
                raise ValueError('invalid GNU build ID')
            source.seek(section[4] + 12 + ((names + 3) & ~3))
            return source.read(length).hex()

    def function_address(self, name_contains=None):
        symbols = self.sections['.symtab']
        with self.path.open('rb') as source, self.path.open('rb') as names:
            source.seek(symbols[4])
            for _ in range(symbols[5] // 24):
                name, info, _, index, value, size = struct.unpack('<IBBHQQ', source.read(24))
                if info & 15 == 2 and index and value and size and name:
                    if name_contains:
                        names.seek(self.sections['.strtab'][4] + name)
                        if name_contains not in names.read(1024).split(b'\0', 1)[0]:
                            continue
                    return value
        raise ValueError('no symbolication probe function')


def compare(original, packaged):
    before, after = Elf(original), Elf(packaged)
    forbidden = [name for name in after.sections if name.startswith(('.debug_', '.zdebug_'))
                 or name in ('.symtab', '.strtab', '.gnu_debugdata')]
    if forbidden:
        raise ValueError('unstripped packaged sections: ' + ', '.join(forbidden))
    if before.build_id() != after.build_id() or before.runtime() != after.runtime():
        raise ValueError('stripping changed native build ID or allocated runtime sections')
    return dict(build_id=after.build_id(), original_bytes=Path(original).stat().st_size,
                packaged_bytes=Path(packaged).stat().st_size, runtime_sections_identical=True,
                load_alignment_4k_16k=True)


def verify_apk(apk, project, archive, revision, ndk):
    """Fail closed on actual APK contents; retain originals outside the repository."""
    symbols = Path(archive) / 'native-symbols'
    symbols.mkdir(exist_ok=False)
    staged = Path(project) / 'Intermediate/Android/arm64/gradle/weltall-symbols'
    rows = []
    with tempfile.TemporaryDirectory(dir=archive, prefix='native-verify-') as temporary, zipfile.ZipFile(apk) as package:
        for entry in package.infolist():
            if not (entry.filename.startswith('lib/') and entry.filename.endswith('.so')):
                continue
            parts = Path(entry.filename).parts
            if len(parts) != 3 or parts[1] != 'arm64-v8a':
                raise ValueError('unexpected native ABI/path')
            original = staged / parts[1] / parts[2]
            if not original.is_file():
                raise ValueError('matching pre-strip symbol archive is missing: ' + str(original))
            packed = Path(temporary) / parts[2]
            with package.open(entry) as source, packed.open('wb') as output:
                shutil.copyfileobj(source, output, 1024 * 1024)
            result = compare(original, packed)
            retained = symbols / parts[1] / result['build_id'] / parts[2]
            retained.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, retained)
            result.update(library=entry.filename, apk_compressed_bytes=entry.compress_size,
                          symbols=str(retained.relative_to(archive)))
            if parts[2] == 'libUnreal.so':
                tool = Path(ndk) / 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-symbolizer.exe'
                address = Elf(retained).function_address(b'FGameLibrary')
                probe = subprocess.check_output([str(tool), '--obj=' + str(retained), hex(address)], text=True)
                if not probe.strip() or probe.splitlines()[0] == '??':
                    raise ValueError('offline symbolication failed')
                result['symbolication_probe'] = dict(address=hex(address), result=probe)
            rows.append(result)
    if not rows or not any(row['library'].endswith('/libUnreal.so') for row in rows):
        raise ValueError('APK lacks native game library')
    report = dict(revision=revision, apk=str(apk), libraries=rows,
                  expanded_bytes_saved=sum(row['original_bytes'] - row['packaged_bytes'] for row in rows))
    (Path(archive) / 'native-symbols.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return report
