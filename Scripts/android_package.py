"""Prepare/build a local ARM64 Android APK from a committed Weltall revision.

    python Scripts/android_package.py --check-toolchain
    python Scripts/android_package.py --prepare-only
    python Scripts/android_package.py --include-discs ../bin --prepare-only
    python Scripts/android_package.py --include-discs ../bin --configuration Development

UE_ROOT is read from the environment or local.env. Install UE 5.8's matching Android
SDK/NDK/JDK with Turnkey first. The export, local disc copies, build logs and APKs
remain under ignored out/android/. No file is ever copied back into the checkout.

The default APK contains code and authored content only. --include-discs is an
explicit local mode: it validates two CUE sheets and their BIN tracks and includes
those files inside the APK. Android verifies them once, then reads directly from
packaged data. Byte-verified old extracted copies are reclaimed without touching
saves/settings. Both-disc APKs and private symbol archives stay local.
"""

import argparse
from datetime import datetime, timezone
import filecmp
import hashlib
import io
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import uuid

from android_engine_fix import needs_volume_time_fix, needs_exec_commands_fix
from android_native_symbols import verify_apk


REPO = Path(__file__).resolve().parent.parent
OUTPUT = REPO / "out" / "android"
PROJECT = OUTPUT / "project"
PROJECT_PATHS = ("Weltall.uproject", "Config", "Content/Story", "Shaders", "Source")
CHUNK_BYTES = 1024 * 1024
MANIFEST_HEADER = "WELTALL_BUNDLED_DISCS_V1"
CUE_FILE = re.compile(r'^\s*FILE\s+(?:"([^"]+)"|(\S+))\s+BINARY\s*$', re.IGNORECASE)
TOOL_ENV_KEYS = ("UE_ROOT", "JAVA_HOME", "ANDROID_HOME", "ANDROID_SDK_ROOT", "NDKROOT", "NDK_ROOT", "ANDROID_NDK_HOME")
BUILD_CACHE_DIRS = {"Binaries", "Intermediate", "Saved", "DerivedDataCache"}


def setting(key):
    if os.environ.get(key):
        return os.environ[key]
    env_file = REPO / "local.env"
    if env_file.is_file():
        for line in env_file.read_text(encoding="utf-8").splitlines():
            name, separator, value = line.partition("=")
            if separator and name.strip() == key and not line.lstrip().startswith("#"):
                return value.strip()
    return None


def checked_relative(name):
    """One portable relative filename, safe in the manifest and on Android."""
    normalized = name.replace("\\", "/")
    path = PurePosixPath(normalized)
    if (not normalized or path.is_absolute() or PureWindowsPath(name).drive
            or any(part in ("", ".", "..") for part in normalized.split("/"))
            or any(char in normalized for char in "\r\n\t|:\0")):
        raise ValueError(f"not a portable relative file path: {name!r}")
    return path


def disc_inputs(folder):
    """Two descriptors and only the BIN files they name, preserving relative paths."""
    source = Path(folder).resolve(strict=True)
    if not source.is_dir():
        raise ValueError("--include-discs must name a directory containing both CUE/BIN pairs")
    sheets = sorted(path for path in source.rglob("*") if path.is_file() and path.suffix.lower() == ".cue")
    if len(sheets) != 2:
        raise ValueError(f"expected exactly two CUE sheets, found {len(sheets)}")
    files = {}
    for sheet in sheets:
        # A symlink to outside the selected input directory must not add unrelated data.
        sheet.resolve(strict=True).relative_to(source)
        relative = checked_relative(sheet.relative_to(source).as_posix())
        files[relative] = sheet
        tracks = []
        for line in sheet.read_text(encoding="utf-8-sig").splitlines():
            if not re.match(r"^\s*FILE(?:\s|$)", line, re.IGNORECASE):
                continue
            match = CUE_FILE.fullmatch(line)
            if not match:
                raise ValueError(f"{sheet.name}: unsupported FILE line (expected a BINARY track)")
            track = checked_relative(match.group(1) or match.group(2))
            if track.suffix.lower() != ".bin":
                raise ValueError(f"{sheet.name}: expected a BIN track")
            named = sheet.parent.joinpath(*track.parts)
            named.resolve(strict=True).relative_to(source)
            if not named.is_file():
                raise ValueError(f"{sheet.name}: track is not a file")
            size = named.stat().st_size
            if size == 0 or size % 2352:
                raise ValueError(f"{sheet.name}: BIN size must be a positive multiple of 2352 bytes")
            relative_track = checked_relative(named.relative_to(source).as_posix())
            files[relative_track] = named
            tracks.append(relative_track)
        if not tracks:
            raise ValueError(f"{sheet.name}: no BINARY tracks")
    # Windows resolves paths without case; Android does not. Reject ambiguous descriptors.
    folded = [str(path).casefold() for path in files]
    if len(set(folded)) != len(folded):
        raise ValueError("disc filenames differ only by case")
    return sorted(files.items(), key=lambda item: str(item[0]))


def export_revision(revision, destination):
    """Export only tracked authored project paths; never copy the working directory."""
    archive = subprocess.run(["git", "-C", str(REPO), "archive", "--format=tar", revision, *PROJECT_PATHS],
                             capture_output=True, check=True).stdout
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        for entry in tar:
            if not entry.isfile():
                continue
            relative = checked_relative(entry.name)
            # Same policy as the desktop package: unfinished expanded disc 2 is opt-in elsewhere.
            if relative.as_posix().startswith("Content/Story/disc2/"):
                continue
            target = destination.joinpath(*relative.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with tar.extractfile(entry) as source, target.open("wb") as output:
                shutil.copyfileobj(source, output, CHUNK_BYTES)


def refresh_project(revision, destination):
    """Refresh committed inputs without destroying the monolithic Android engine cache."""
    root = destination.resolve()

    def remove(path):
        if path.is_symlink():
            path.unlink()  # Remove the link itself, never its target.
            return
        resolved = path.resolve()
        if resolved == root or not resolved.is_relative_to(root):
            raise ValueError("refusing to remove an export path outside the Android project")
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink()

    def sync(source, target):
        if target.is_symlink():
            remove(target)
        if not target.resolve().is_relative_to(root):
            raise ValueError("an Android export directory points outside the project")
        if target.exists() and not target.is_dir():
            remove(target)
        target.mkdir(parents=True, exist_ok=True)
        incoming = {path.name: path for path in source.iterdir()}
        for old in target.iterdir():
            if target == root and old.name in BUILD_CACHE_DIRS:
                if not old.is_dir() or not old.resolve().is_relative_to(root):
                    raise ValueError("an Android build cache is not a project-local directory")
                continue
            if old.name not in incoming:
                remove(old)
        for name, new in incoming.items():
            old = target / name
            if new.is_dir():
                sync(new, old)
            else:
                if old.is_symlink() or (old.exists() and not old.is_file()):
                    remove(old)
                if not old.is_file() or not filecmp.cmp(new, old, shallow=False):
                    shutil.copyfile(new, old)

    OUTPUT.mkdir(parents=True, exist_ok=True)
    # Complete git archive first: an invalid revision must leave the existing build intact.
    with tempfile.TemporaryDirectory(prefix="source-", dir=OUTPUT) as temporary:
        incoming = Path(temporary)
        export_revision(revision, incoming)
        sync(incoming, root)


def stage_discs(inputs, destination):
    """Streaming copies plus SHA-1 transfer-integrity manifest; memory use is bounded."""
    destination.mkdir(parents=True, exist_ok=True)
    manifest = [MANIFEST_HEADER]
    required = sum(path.stat().st_size for _, path in inputs)
    if shutil.disk_usage(destination).free < required + 256 * 1024 * 1024:
        raise ValueError("not enough space for the local disc staging copy and 256 MiB headroom")
    for relative, source in inputs:
        target = destination.joinpath(*relative.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        digest = hashlib.sha1()
        length = 0
        # Core CUE keywords are uppercase and Android uses '/' even when the source came from Windows.
        if source.suffix.lower() == ".cue":
            incoming = io.BytesIO(portable_cue(source).encode("utf-8"))
        else:
            incoming = source.open("rb")
        with incoming, target.open("wb") as outgoing:
            while block := incoming.read(CHUNK_BYTES):
                outgoing.write(block)
                digest.update(block)
                length += len(block)
        if source.suffix.lower() != ".cue" and length != source.stat().st_size:
            raise ValueError("disc input changed while being copied")
        manifest.append(f"{digest.hexdigest()}|{length}|{relative.as_posix()}")
    (destination / "Manifest.txt").write_text("\n".join(manifest) + "\n", encoding="utf-8")
    return required


def portable_cue(source):
    """Normalize descriptor syntax only; BIN image bytes are always copied unchanged."""
    lines = []
    for line in source.read_text(encoding="utf-8-sig").splitlines():
        file = CUE_FILE.fullmatch(line)
        keyword = re.match(r"^\s*(TRACK|INDEX)\s+(.*)$", line, re.IGNORECASE)
        if file:
            name = checked_relative(file.group(1) or file.group(2)).as_posix()
            lines.append(f'FILE "{name}" BINARY')
        elif keyword:
            lines.append(f"  {keyword.group(1).upper()} {keyword.group(2).upper()}")
        else:
            lines.append(line)
    return "\n".join(lines) + "\n"


def configure_local_discs(project):
    config = project / "Config" / "Android" / "AndroidGame.ini"
    config.parent.mkdir(parents=True, exist_ok=True)
    with config.open("a", encoding="utf-8", newline="\n") as output:
        output.write('\n[/Script/UnrealEd.ProjectPackagingSettings]\n'
                     '+DirectoriesToAlwaysStageAsUFS=(Path="BundledDiscs")\n')


def stage_storage_fixtures(project):
    """Generated test data only; the second stream is deliberately incompressible."""
    folder = project / "Content/StorageFixtures"
    folder.mkdir(parents=True, exist_ok=True)
    size = 3 * 65536 + 17
    (folder / "Compressed.bin").write_bytes(bytes(i % 251 for i in range(size)))
    state = 1
    random = bytearray()
    for _ in range(size):
        state = (1664525 * state + 1013904223) & 0xffffffff
        random.append(state >> 24)
    (folder / "Raw.bin").write_bytes(random)
    config = project / "Config/Android/AndroidGame.ini"
    config.parent.mkdir(parents=True, exist_ok=True)
    with config.open("a", encoding="utf-8") as output:
        output.write('\n[/Script/UnrealEd.ProjectPackagingSettings]\n'
                     '+DirectoriesToAlwaysStageAsUFS=(Path="StorageFixtures")\n')


def stage_build_identity(project, revision):
    folder = project / "Content/AndroidBuild"
    folder.mkdir(parents=True, exist_ok=True)
    (folder/"Revision.txt").write_text(revision[:12] + "\n", encoding="ascii")
    config = project / "Config/Android/AndroidGame.ini"
    config.parent.mkdir(parents=True, exist_ok=True)
    with config.open("a", encoding="utf-8") as output:
        output.write('\n[/Script/UnrealEd.ProjectPackagingSettings]\n'
                     '+DirectoriesToAlwaysStageAsUFS=(Path="AndroidBuild")\n')


def configure_distribution(project):
    """Signing material stays external; only the disposable export receives local credentials."""
    values = {key: setting("WELTALL_RELEASE_" + key) for key in ("KEYSTORE", "KEY_ALIAS", "STORE_PASSWORD", "KEY_PASSWORD")}
    if not all(values.values()):
        raise ValueError("configure local WELTALL_RELEASE_KEYSTORE, KEY_ALIAS, STORE_PASSWORD and KEY_PASSWORD settings")
    keystore = Path(values["KEYSTORE"]).resolve(strict=True)
    if not keystore.is_file() or keystore.is_relative_to(project.resolve()):
        raise ValueError("release keystore must be a private file outside the disposable project export")
    for key in ("KEY_ALIAS", "STORE_PASSWORD", "KEY_PASSWORD"):
        if any(char in values[key] for char in "\r\n'\\"):
            raise ValueError("local signing values cannot contain quotes, backslashes or newlines")
    config = project / "Config/Android/AndroidEngine.ini"
    config.parent.mkdir(parents=True, exist_ok=True)
    with config.open("a", encoding="utf-8") as output:
        output.write('\n[/Script/AndroidRuntimeSettings.AndroidRuntimeSettings]\n'
                     f'KeyStore={keystore.as_posix()}\nKeyAlias={values["KEY_ALIAS"]}\n'
                     f'KeyStorePassword={values["STORE_PASSWORD"]}\nKeyPassword={values["KEY_PASSWORD"]}\n')
    config = project / "Config/Android/AndroidGame.ini"
    with config.open("a", encoding="utf-8") as output:
        output.write('\n[/Script/UnrealEd.ProjectPackagingSettings]\n'
                     '+IniKeyDenylist=KeyStore\n+IniKeyDenylist=KeyAlias\n'
                     '+IniKeyDenylist=KeyStorePassword\n+IniKeyDenylist=KeyPassword\n')


def uat_command(ue_root, project, archive, configuration, distribution=False):
    launcher = "RunUAT.bat" if os.name == "nt" else "RunUAT.sh"
    return [str(Path(ue_root) / "Engine" / "Build" / "BatchFiles" / launcher), "-WaitForUATMutex", "BuildCookRun",
            f"-project={project / 'Weltall.uproject'}", "-platform=Android", "-cookflavor=ASTC",
            f"-clientconfig={configuration}", "-build", "-cook", "-stage", "-pak", "-package", "-archive",
            # The host cooker reads host packaging defaults, not AndroidGame.ini. Its output
            # must match Android's legacy pak staging rather than UE 5.8's default Zen store.
            "-AdditionalCookerOptions=-SkipZenStore",
            f"-archivedirectory={archive}", "-nodebuginfo", "-SkipCookingEditorContent",
            "-noP4", "-unattended", "-utf8output"] + (["-distribution"] if distribution else [])


def toolchain_problems(ue_root):
    if not ue_root:
        return ["UE_ROOT is not configured in the environment or local.env (UE 5.8 is required)"]
    engine = Path(ue_root) / "Engine"
    launcher = engine / "Build" / "BatchFiles" / ("RunUAT.bat" if os.name == "nt" else "RunUAT.sh")
    problems = []
    if not launcher.is_file():
        problems.append(f"Unreal Automation Tool not found: {launcher}")
    version = engine / "Build" / "Build.version"
    if version.is_file():
        import json
        values = json.loads(version.read_text(encoding="utf-8-sig"))
        if (values.get("MajorVersion"), values.get("MinorVersion")) != (5, 8):
            problems.append("UE_ROOT must point to UE 5.8 for this project")
    if needs_volume_time_fix(ue_root):
        problems.append("UE Android volume callback needs the thread-safe clock correction: "
                        "run python Scripts/android_engine_fix.py --apply, then rebuild")
    if needs_exec_commands_fix(ue_root):
        problems.append("UE Android embedded exec needs the Shipping command guard: "
                        "run python Scripts/android_engine_fix.py --apply, then rebuild")
    return problems


def build_environment():
    """Scope approved local tool paths to child processes, preserving environment precedence."""
    environment = os.environ.copy()
    for key in TOOL_ENV_KEYS:
        value = setting(key)
        if value:
            environment[key] = value
    return environment


def run_build(command, log):
    log.parent.mkdir(parents=True, exist_ok=True)
    kwargs = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    with log.open("w", encoding="utf-8", newline="\n") as output:
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              encoding="utf-8", errors="replace", env=build_environment(), **kwargs) as process:
            for line in process.stdout:
                output.write(line)
                output.flush()
                if any(mark in line for mark in ("Error:", "ERROR:", "BUILD SUCCESSFUL", "BUILD FAILED")):
                    print(line.rstrip())
            return process.wait()


def check_export_inputs(inputs, destination):
    for _, source in inputs:
        if source.resolve().is_relative_to(destination.resolve()):
            raise ValueError("disc inputs cannot be inside the disposable Android project export; move them to ../bin first")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--configuration", choices=("Development", "Shipping"), default="Development")
    parser.add_argument("--distribution", action="store_true", help="sign a non-debuggable Shipping APK with private local release settings; no store upload")
    parser.add_argument("--revision", default="HEAD", help="committed revision to export (default: HEAD)")
    parser.add_argument("--include-discs", type=Path, metavar="FOLDER", help="include both local CUE/BIN pairs in the local APK")
    parser.add_argument("--prepare-only", action="store_true", help="prepare local export without Unreal build tools")
    parser.add_argument("--clean-export", action="store_true", help="discard compiled caches and prepare a completely fresh project")
    parser.add_argument("--check-toolchain", action="store_true", help="report engine readiness without staging or building")
    parser.add_argument("--print-plan", action="store_true", help="validate inputs and show locations without writing files")
    args = parser.parse_args(argv)
    ue_root = setting("UE_ROOT")
    if args.check_toolchain:
        problems = toolchain_problems(ue_root)
        for problem in problems:
            print(problem)
        if not problems:
            print("UE 5.8 found. Android SDK/NDK/JDK validation is performed by UE Turnkey at build time.")
        return int(bool(problems))
    try:
        if args.distribution and args.configuration != "Shipping":
            raise ValueError("--distribution requires --configuration Shipping")
        inputs = disc_inputs(args.include_discs) if args.include_discs else []
        check_export_inputs(inputs, PROJECT)
        required = sum(path.stat().st_size for _, path in inputs)
        print(f"Export: {PROJECT}")
        print(f"Build outputs: {OUTPUT / 'packages' / args.configuration}")
        print(f"Bundled inputs: {len(inputs)} files, {required / 2**20:.1f} MiB")
        if args.print_plan:
            return 0
        if not args.prepare_only:
            problems = toolchain_problems(ue_root)
            if problems:
                raise ValueError("; ".join(problems))
        # Guard the absolute target before any export cleanup.
        destination = PROJECT.resolve()
        expected = (REPO.resolve() / "out" / "android" / "project")
        if destination != expected or not destination.is_relative_to(REPO.resolve()):
            raise ValueError("refusing to replace a packaging directory outside out/android/project")
        if args.clean_export and destination.exists():
            shutil.rmtree(destination)
        revision = subprocess.check_output(["git", "-C", str(REPO), "rev-parse", args.revision], text=True).strip()
        refresh_project(revision, destination)
        stage_build_identity(destination, revision)
        if inputs:
            stage_discs(inputs, destination / "Content" / "BundledDiscs")
            configure_local_discs(destination)
        if args.configuration == "Development":
            stage_storage_fixtures(destination)
        if args.distribution:
            configure_distribution(destination)
        print("Local export prepared." + (" Both discs will be inside the APK." if inputs else " No discs bundled."))
        if args.prepare_only:
            return 0
        # A fresh archive directory prevents an old APK from satisfying this build's success check.
        build_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
        archive = OUTPUT / "packages" / args.configuration / build_id
        archive.mkdir(parents=True, exist_ok=False)
        log = OUTPUT / "logs" / f"{args.configuration}-{build_id}.log"
        status = run_build(uat_command(ue_root, destination, archive, args.configuration, args.distribution), log)
        if status:
            print(f"Android build failed ({status}); see {log}", file=sys.stderr)
            return status
        apks = sorted(archive.rglob("*.apk"))
        if not apks:
            raise ValueError("Unreal reported success but the archive contains no APK")
        ndk = setting("NDKROOT") or setting("NDK_ROOT") or setting("ANDROID_NDK_HOME")
        if not ndk:
            raise ValueError("NDKROOT is required to verify archived native symbols")
        for apk in apks:
            report = verify_apk(apk, destination, apk.parent, revision, ndk)
            print(f"Verified stripped native libraries: {report['expanded_bytes_saved']:,} expanded bytes removed")
        print("APK generated; device installation and gameplay validation are still required.")
        for apk in apks:
            print(apk)
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"android_package: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
