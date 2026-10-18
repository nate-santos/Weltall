#!/usr/bin/env python3
"""Keep player data and generated packages out of Git, including forced additions.

    python Scripts/repo_guard.py --staged       # complete index, as a commit sees it
    python Scripts/repo_guard.py --tracked      # committed HEAD snapshot
    python Scripts/repo_guard.py --revision HEAD
    python Scripts/repo_guard.py --pre-push origin  # Git pre-push input on stdin

Only Git objects are inspected: unstaged files and external disc paths are never
opened. Large blobs are rejected by size before any content is requested. This
is a guard against accidental additions, not a copyright or provenance audit.
New assets still need to be original or appropriately licensed.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
from typing import BinaryIO


MAX_BLOB_BYTES = 8 * 1024 * 1024
INSPECT_BYTES = 64 * 1024
FORBIDDEN_EXTENSIONS = frozenset({
    # Disc images, disc descriptors, PlayStation programs and memory cards.
    ".bin", ".cue", ".chd", ".iso", ".img", ".pbp", ".ecm", ".mdf",
    ".mds", ".ccd", ".sub", ".nrg", ".cso", ".zso", ".psx", ".ps-exe",
    ".mcr", ".mcd", ".gme", ".srm", ".sav", ".sstate", ".state",
    # Packaged games, executables, archives, cooked assets and compiler output.
    ".apk", ".aab", ".apks", ".xapk", ".obb", ".pak", ".ucas", ".utoc",
    ".exe", ".dll", ".so", ".dylib", ".o", ".obj", ".a", ".lib", ".pdb",
    ".zip", ".7z", ".rar", ".gz", ".bz2", ".xz", ".tar", ".zst",
    ".dmp", ".log", ".profraw", ".profdata", ".trace", ".mp4", ".webm",
})
FORBIDDEN_DIRECTORIES = frozenset({
    "games", "assets", "bin", "binaries", "intermediate", "saved",
    "deriveddatacache", "out", "output", "outputs", "testing", "test-results",
    "test-artifacts", "captures", "screenshots", "recordings", "dumps", "logs",
    "build", "builds", "dist", "artifacts", "staging", "packaged",
    "__pycache__", ".gradle", ".cxx", ".externalnativebuild",
})
CD_SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
CUE_FILE = re.compile(rb'^\s*FILE\s+.+\s+(?:BINARY|MOTOROLA|WAVE|AIFF|MP3)\s*$', re.I | re.M)
CUE_TRACK = re.compile(rb'^\s*TRACK\s+\d+\s+(?:MODE[12]/\d+|AUDIO)\s*$', re.I | re.M)


class GuardError(Exception):
    """Git could not provide a complete snapshot; fail closed."""


@dataclass(frozen=True)
class Entry:
    path: str
    oid: str
    mode: str


def path_problem(path: str) -> str | None:
    normalized = path.replace("\\", "/").casefold()
    parts = PurePosixPath(normalized).parts
    if any(part in FORBIDDEN_DIRECTORIES for part in parts[:-1]):
        return "local game data or generated output directory"
    if PurePosixPath(normalized).suffix in FORBIDDEN_EXTENSIONS:
        return "disc, save, package, archive or generated binary extension"
    if normalized.endswith("_builtdata.uasset"):
        return "generated Unreal lighting data"
    if any(part.startswith(("cmake-build-", "build-")) for part in parts[:-1]):
        return "generated build directory"
    return None


def content_problem(prefix: bytes, size: int) -> str | None:
    if size > MAX_BLOB_BYTES:
        return f"blob exceeds the {MAX_BLOB_BYTES // (1024 * 1024)} MiB source limit"
    signatures = (
        (b"MComprHD", "CHD disc image"),
        (b"PS-X EXE", "PlayStation executable"),
        (b"\x00PBP", "PBP game package"),
        (b"ECM\x00", "ECM disc image"),
        (b"CISO", "compressed disc image"),
        (b"ZISO", "compressed disc image"),
        (b"PK\x03\x04", "ZIP/APK archive"),
        (b"PK\x05\x06", "ZIP/APK archive"),
        (b"7z\xbc\xaf\x27\x1c", "7-Zip archive"),
        (b"Rar!\x1a\x07", "RAR archive"),
        (b"\x1f\x8b", "gzip archive"),
        (b"\xfd7zXZ\x00", "XZ archive"),
        (b"\x7fELF", "compiled ELF binary"),
        (b"version https://git-lfs.github.com/spec/v1", "Git LFS pointer"),
    )
    for signature, description in signatures:
        if prefix.startswith(signature):
            return description + " content (regardless of its filename)"
    if prefix.startswith(CD_SYNC):
        return "raw CD sector content (regardless of its filename)"
    if prefix.startswith(b"MZ") and len(prefix) >= 64:
        pe_offset = int.from_bytes(prefix[60:64], "little")
        if prefix[pe_offset:pe_offset + 4] == b"PE\x00\x00":
            return "compiled Windows executable content"
    # ISO descriptors in cooked 2048-byte sectors and 2352-byte Mode 1/2 sectors.
    for sector_size, data_offset in ((2048, 0), (2352, 16), (2352, 24)):
        offset = 16 * sector_size + data_offset
        if prefix[offset + 1:offset + 6] == b"CD001":
            return "ISO9660 disc content (regardless of its filename)"
    if size == 128 * 1024 and prefix.startswith(b"MC"):
        return "PlayStation memory card content"
    if CUE_FILE.search(prefix) and CUE_TRACK.search(prefix):
        return "CUE disc descriptor content (regardless of its filename)"
    return None


def git(repo: Path, *arguments: str, input_data: bytes | None = None) -> bytes:
    result = subprocess.run(
        ["git", "-C", str(repo), *arguments], input=input_data,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
    )
    if result.returncode:
        # Avoid echoing arbitrary content or paths from Git's diagnostic output.
        raise GuardError("Git failed while reading repository metadata: " + arguments[0])
    return result.stdout


def index_entries(repo: Path) -> list[Entry]:
    entries = []
    for record in git(repo, "ls-files", "--stage", "-z").split(b"\0"):
        if not record:
            continue
        metadata, raw_path = record.split(b"\t", 1)
        mode, oid, stage = metadata.decode("ascii").split()
        if stage != "0":
            raise GuardError("Resolve all index conflicts before committing.")
        entries.append(Entry(raw_path.decode("utf-8", "surrogateescape"), oid, mode))
    return entries


def revision_entries(repo: Path, revision: str) -> list[Entry]:
    entries = []
    # Resolve first so user-supplied refs cannot be interpreted as ls-tree options.
    commit = git(repo, "rev-parse", "--verify", revision + "^{commit}").strip().decode("ascii")
    for record in git(repo, "ls-tree", "-r", "-z", "--full-tree", commit).split(b"\0"):
        if not record:
            continue
        metadata, raw_path = record.split(b"\t", 1)
        mode, _kind, oid = metadata.decode("ascii").split()
        entries.append(Entry(raw_path.decode("utf-8", "surrogateescape"), oid, mode))
    return entries


class BlobReader:
    """Read bounded prefixes from Git blobs, never the caller's working files."""

    def __init__(self, repo: Path):
        self.repo = repo
        self.process: subprocess.Popen[bytes] | None = None
        self.problems: dict[str, str | None] = {}

    def __enter__(self) -> BlobReader:
        self.process = subprocess.Popen(
            ["git", "-C", str(self.repo), "cat-file", "--batch"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        )
        return self

    def __exit__(self, *_args: object) -> None:
        if self.process:
            if self.process.stdin:
                self.process.stdin.close()
            if self.process.stdout:
                self.process.stdout.close()
            self.process.wait()

    def inspect(self, oid: str, size: int) -> str | None:
        if oid in self.problems:
            return self.problems[oid]
        if size > MAX_BLOB_BYTES:
            problem = content_problem(b"", size)
        else:
            assert self.process and self.process.stdin and self.process.stdout
            self.process.stdin.write(oid.encode("ascii") + b"\n")
            self.process.stdin.flush()
            metadata = self.process.stdout.readline().split()
            if len(metadata) != 3 or metadata[1] != b"blob" or int(metadata[2]) != size:
                raise GuardError("Git could not read an expected blob.")
            prefix = read_exact(self.process.stdout, min(size, INSPECT_BYTES))
            remaining = size - len(prefix)
            while remaining:
                remaining -= len(read_exact(self.process.stdout, min(remaining, INSPECT_BYTES)))
            if self.process.stdout.read(1) != b"\n":
                raise GuardError("Unexpected end of a Git blob.")
            problem = content_problem(prefix, size)
        self.problems[oid] = problem
        return problem


def read_exact(stream: BinaryIO, count: int) -> bytes:
    chunks = []
    while count:
        chunk = stream.read(count)
        if not chunk:
            raise GuardError("Unexpected end of Git output.")
        chunks.append(chunk)
        count -= len(chunk)
    return b"".join(chunks)


def inspect_entries(repo: Path, entries: list[Entry], reader: BlobReader) -> list[str]:
    problems = []
    candidates = []
    for entry in entries:
        problem = path_problem(entry.path)
        if problem:
            problems.append(f"{entry.path!r}: {problem}")
        elif entry.mode not in {"100644", "100755"}:
            # A submodule or symlink could silently point to external player data.
            problems.append(f"{entry.path!r}: external links and submodules are not accepted in source snapshots")
        else:
            candidates.append(entry)
    if candidates:
        oids = list(dict.fromkeys(entry.oid for entry in candidates))
        metadata = git(repo, "cat-file", "--batch-check", input_data=("\n".join(oids) + "\n").encode("ascii"))
        sizes = {}
        for line in metadata.splitlines():
            fields = line.split()
            if len(fields) != 3 or fields[1] != b"blob":
                raise GuardError("Git could not inspect an expected blob.")
            sizes[fields[0].decode("ascii")] = int(fields[2])
        for entry in candidates:
            problem = reader.inspect(entry.oid, sizes[entry.oid])
            if problem:
                problems.append(f"{entry.path!r}: {problem}")
    return problems


def outgoing_commits(repo: Path, remote: str, updates: str) -> list[str]:
    commits: set[str] = set()
    for line in updates.splitlines():
        fields = line.split()
        if len(fields) != 4:
            raise GuardError("Invalid pre-push input from Git.")
        _local_ref, local_oid, _remote_ref, remote_oid = fields
        if not re.fullmatch(r"[0-9a-fA-F]{40}|[0-9a-fA-F]{64}", local_oid):
            raise GuardError("Invalid local object ID in pre-push input.")
        if set(local_oid) == {"0"}:
            continue  # Deleting a remote ref introduces no files.
        if set(remote_oid) != {"0"}:
            if not re.fullmatch(r"[0-9a-fA-F]{40}|[0-9a-fA-F]{64}", remote_oid):
                raise GuardError("Invalid remote object ID in pre-push input.")
            arguments = ("rev-list", local_oid, "^" + remote_oid)
        else:
            arguments = ("rev-list", local_oid, "--not", "--remotes=" + remote)
        for commit in git(repo, *arguments).decode("ascii").splitlines():
            commits.add(commit)
    return sorted(commits)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parent.parent)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--staged", action="store_true", help="inspect the complete index (default)")
    mode.add_argument("--tracked", action="store_true", help="inspect the committed HEAD tree")
    mode.add_argument("--revision", help="inspect a specific commit's tree")
    mode.add_argument("--commit-range", help="inspect every commit in a Git revision range (for CI)")
    mode.add_argument("--pre-push", metavar="REMOTE", help="inspect every outgoing commit using Git's stdin protocol")
    args = parser.parse_args()
    try:
        with BlobReader(args.repo) as reader:
            if args.pre_push is not None:
                snapshots = ((commit, revision_entries(args.repo, commit)) for commit in outgoing_commits(args.repo, args.pre_push, sys.stdin.read()))
            elif args.commit_range is not None:
                commits = git(args.repo, "rev-list", "--end-of-options", args.commit_range).decode("ascii").splitlines()
                snapshots = ((commit, revision_entries(args.repo, commit)) for commit in commits)
            elif args.revision is not None or args.tracked:
                revision = args.revision if args.revision is not None else "HEAD"
                snapshots = [(revision, revision_entries(args.repo, revision))]
            else:
                snapshots = [("staged snapshot", index_entries(args.repo))]
            failures = []
            snapshot_count = 0
            for label, entries in snapshots:
                snapshot_count += 1
                failures.extend(f"{label}: {problem}" for problem in inspect_entries(args.repo, entries, reader))
        if failures:
            print("Repository data guard rejected these Git objects:", file=sys.stderr)
            for failure in failures:
                print("  " + failure, file=sys.stderr)
            print("Keep discs, saves, captures and builds in ignored local folders. Unstage these files before committing.", file=sys.stderr)
            return 1
        print(f"Repository data guard passed ({snapshot_count} snapshot(s)).")
        return 0
    except (GuardError, OSError, ValueError) as error:
        print("Repository data guard could not complete: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
