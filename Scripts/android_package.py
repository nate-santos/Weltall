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
those files inside the APK. The first Android launch streams them to private app
storage for the existing native disc reader. This mode needs space for the APK
and an additional uncompressed copy on the phone; these APKs stay local.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import io
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import shutil
import subprocess
import sys
import tarfile
import uuid


REPO = Path(__file__).resolve().parent.parent
OUTPUT = REPO / "out" / "android"
PROJECT = OUTPUT / "project"
PROJECT_PATHS = ("Weltall.uproject", "Config", "Content/Story", "Shaders", "Source")
CHUNK_BYTES = 1024 * 1024
MANIFEST_HEADER = "WELTALL_BUNDLED_DISCS_V1"
CUE_FILE = re.compile(r'^\s*FILE\s+(?:"([^"]+)"|(\S+))\s+BINARY\s*$', re.IGNORECASE)


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


def uat_command(ue_root, project, archive, configuration):
    launcher = "RunUAT.bat" if os.name == "nt" else "RunUAT.sh"
    return [str(Path(ue_root) / "Engine" / "Build" / "BatchFiles" / launcher), "-WaitForUATMutex", "BuildCookRun",
            f"-project={project / 'Weltall.uproject'}", "-platform=Android", "-cookflavor=ASTC",
            f"-clientconfig={configuration}", "-build", "-cook", "-stage", "-pak", "-package", "-archive",
            f"-archivedirectory={archive}", "-nodebuginfo", "-SkipCookingEditorContent",
            "-noP4", "-unattended", "-utf8output"]


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
    return problems


def run_build(command, log):
    log.parent.mkdir(parents=True, exist_ok=True)
    kwargs = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    with log.open("w", encoding="utf-8", newline="\n") as output:
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              encoding="utf-8", errors="replace", **kwargs) as process:
            for line in process.stdout:
                output.write(line)
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
    parser.add_argument("--revision", default="HEAD", help="committed revision to export (default: HEAD)")
    parser.add_argument("--include-discs", type=Path, metavar="FOLDER", help="include both local CUE/BIN pairs in the local APK")
    parser.add_argument("--prepare-only", action="store_true", help="prepare local export without Unreal build tools")
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
        # Guard the absolute target before recursive removal. This one fixed export is disposable.
        destination = PROJECT.resolve()
        expected = (REPO.resolve() / "out" / "android" / "project")
        if destination != expected or not destination.is_relative_to(REPO.resolve()):
            raise ValueError("refusing to replace a packaging directory outside out/android/project")
        if destination.exists():
            shutil.rmtree(destination)
        export_revision(args.revision, destination)
        if inputs:
            stage_discs(inputs, destination / "Content" / "BundledDiscs")
            configure_local_discs(destination)
        print("Local export prepared." + (" Both discs will be inside the APK." if inputs else " No discs bundled."))
        if args.prepare_only:
            return 0
        # A fresh archive directory prevents an old APK from satisfying this build's success check.
        build_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
        archive = OUTPUT / "packages" / args.configuration / build_id
        archive.mkdir(parents=True, exist_ok=False)
        log = OUTPUT / "logs" / f"{args.configuration}-{build_id}.log"
        status = run_build(uat_command(ue_root, destination, archive, args.configuration), log)
        if status:
            print(f"Android build failed ({status}); see {log}", file=sys.stderr)
            return status
        apks = sorted(archive.rglob("*.apk"))
        if not apks:
            raise ValueError("Unreal reported success but the archive contains no APK")
        print("APK generated; device installation and gameplay validation are still required.")
        for apk in apks:
            print(apk)
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"android_package: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
