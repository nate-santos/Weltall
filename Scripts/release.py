"""Builds a release of the game and drafts it on GitHub.

    python Scripts/release.py 0.8.0                      # both Shipping packages, their zips, a draft release
    python Scripts/release.py 0.8.0 --archives-only      # the zips alone, to look at before drafting
    python Scripts/release.py 0.8.0 --platforms Windows  # one platform

The release is built from HEAD, which must be clean, already pushed to GitHub (the draft points at it) and carry the
version in Config/DefaultGame.ini's ProjectVersion. Each platform's Shipping package is built, run and archived by
package.py; its zips are then named by the version in out/releases: Weltall-<version>-<Platform>.zip for players and
...-symbols.zip, which stays with whoever builds releases (it turns a player's crash report into source lines). The
notes go beside them as Weltall-<version>-notes.md.

Without --archives-only a draft release is made with the GitHub CLI (gh, logged in), the players' zips attached and
the notes as its text, and the commit is tagged v<version> here. Nothing is published: the draft waits on the
release page to be read, edited and published by hand, which creates the tag on GitHub. GitHub attaches the tag's
source code to the release by itself.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RELEASES = REPO / "out" / "releases"
PLATFORMS = ["Windows", "Linux"]
VERSION = re.compile(r"\d+\.\d+\.\d+")
PROJECT_VERSION = re.compile(r"^ProjectVersion=(.*)$", re.MULTILINE)

NOTES = """\
Weltall {version} is a native port of *Xenogears* (PlayStation, 1998), rebuilt in Unreal Engine 5.
You'll need your own copy of the USA release or a supported English undub. No game data is included.

## What's new

- PlayStation controllers now work on Windows: the DualShock 4 and the DualSense, including the Edge, over USB or
  Bluetooth, and the PlayStation Classic controller. Sony's wireless adapter for the DualShock 4 works too. With
  them, button prompts show the PlayStation symbols, and the DualShock 4 and DualSense vibrate in the Battling arena.
  Most other gamepads work too. If a button lands in the wrong place, rebind it on the Controls page. If your
  controller isn't recognized, please report it.
- Fan translations that only change the game's text, pictures and movies now play, such as the Spanish translation by
  Romhack Hispano. Patch your USA discs and put the patched images in `Games`. If the originals are there too, the
  patched ones play. Weltall's own screens, such as Options, stay in English. Translations that also change the
  game's program, such as the French one by Atelier Traduction, aren't supported yet.
- Added separate volumes under Options → Sound for the music, sound effects, battle voices and movies, plus a master
  volume.
- You can now use the movie subtitles from your own copy of Perfect Works Build, even with the mod turned off. If you
  have the undub too, Subtitles from under Options → Sound picks which one you read. The Sound page now shows what
  it found, and says what you need for anything that's missing.

### Fixes

- Fixed the camera shaking when you turn or zoom it slowly on the field.
- Fixed pieces of character sprites, such as Fei's arm when he runs away from the camera, sitting a few pixels off.
  This is a mistake in the original game's sprites, and the Sprite frames option on the Fixes page turns the fix off.
- Fixed the rainy night scene near the start of the game using far more of the graphics card than it should.
  Battles and the world map are lighter on the graphics card too.
- Fixed the display's native resolution missing from the list, and the picture being stretched, when Windows display
  scaling is above 100%. If you picked a window size while scaling was on, it will look smaller now, so pick it again.

## Playing

1. Download the Windows or Linux zip below and unzip it wherever you want.
2. Put both disc images in the `Games` folder. BIN/CUE and CHD preserve movie audio; ISO/IMG also work,
   but the movies will be silent.
3. Run `Weltall.exe` on Windows 10 or 11 (64-bit, DirectX 12), or `Weltall.sh` on Linux (64-bit, Vulkan).

On Windows, settings and saves are in `%LOCALAPPDATA%\\Weltall`. On Linux, settings are in
`~/.config/Weltall` and saves are in `~/.local/share/Weltall/Saves`. You can import PlayStation memory-card
saves from the Saves page in Options. See the [README](https://github.com/Roman215/Weltall#playing) for
controls and more details.

## What to expect

Both discs are playable, and gameplay behavior has been checked against the original game's code.
My own playthrough is still in progress, with over 18 hours played without a hang. Automated tests
have run every map and story battle, along with long stretches of the story from save to save. You
should be able to play from start to finish, and I'm fixing reported issues as they come in.

Players have run the Linux build on a Steam Deck and on CachyOS with an AMD graphics card. If you run
into problems on Linux, please report them.

If the game stops with an error on screen, attach the log file it names to a
[bug report](https://github.com/Roman215/Weltall/issues/new?template=bug_report.md) and describe what you
were doing. If it crashes, zip and attach the newest folder in `%LOCALAPPDATA%\\Weltall\\Saved\\Crashes`
(Windows) or `~/.config/Epic/Weltall/Saved/Crashes` (Linux). A save from just before the problem helps too.
"""


def unwrapped(text):
    """Text with each paragraph and list item on one line: GitHub shows a release's notes with every line break kept."""
    lines = []
    for line in text.splitlines():
        joins = lines and lines[-1].strip() and line.strip() and not line.startswith("#") and not lines[-1].startswith("#")
        if joins and not re.match(r"(\d+\.|[-*]) ", line):
            lines[-1] += " " + line.strip()
        else:
            lines.append(line)
    return "\n".join(lines) + "\n"


def git(*arguments):
    return subprocess.run(["git", "-C", str(REPO), *arguments], capture_output=True, text=True, check=True).stdout.strip()


def refuse(message):
    sys.exit(f"release: {message}")


def check_source(version):
    """Refuses unless HEAD is clean, carries the version and has no release tag yet."""
    if git("status", "--porcelain"):
        refuse("the working tree has changes: commit them first")
    ini = (REPO / "Config" / "DefaultGame.ini").read_text(encoding="utf-8")
    found = PROJECT_VERSION.search(ini)
    if found is None or found.group(1).strip() != version:
        refuse(f"Config/DefaultGame.ini's ProjectVersion is {found.group(1).strip() if found else 'missing'}, not {version}")
    if subprocess.run(["git", "-C", str(REPO), "rev-parse", "-q", "--verify", f"refs/tags/v{version}"],
                      capture_output=True).returncode == 0:
        refuse(f"v{version} is tagged already")


def check_pushed(commit):
    """Refuses unless gh is logged in and GitHub has the commit the draft will point at."""
    try:
        found = subprocess.run(["gh", "api", f"repos/{{owner}}/{{repo}}/commits/{commit}", "--silent"], cwd=REPO,
                               capture_output=True, text=True)
    except FileNotFoundError:
        refuse("the GitHub CLI (gh) is not installed")
    if found.returncode != 0:
        refuse(f"GitHub does not have {commit[:7]} (push it first; is gh logged in?): {found.stderr.strip()}")


def build(platform, commit):
    """The platform's Shipping package of commit built, run and archived by package.py; the archives' paths."""
    print(f"release: building the Shipping package for {platform}")
    status = subprocess.run([sys.executable, str(REPO / "Scripts" / "package.py"), "Shipping", "--archive",
                             "--platform", platform, "--revision", commit]).returncode
    if status != 0:
        refuse(f"the {platform} package failed (exit status {status}): see package.py's output above")
    revision = (REPO / "out" / "builds" / "Shipping" / platform / "Revision.txt").read_text(encoding="utf-8").strip()
    if revision != git("rev-parse", "--short", commit):
        refuse(f"the {platform} package is of {revision}, not of {commit[:7]}")
    return RELEASES / f"Weltall-{revision}-{platform}.zip", RELEASES / f"Weltall-{revision}-{platform}-symbols.zip"


def main():
    sys.stdout.reconfigure(line_buffering=True)
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("version", help="the release's version, as ProjectVersion has it (0.8.0)")
    parser.add_argument("--platforms", nargs="+", default=PLATFORMS, choices=PLATFORMS, help="the platforms to release (default: both)")
    parser.add_argument("--archives-only", action="store_true", help="write the zips and the notes, but tag and draft nothing")
    args = parser.parse_args()
    if not VERSION.fullmatch(args.version):
        parser.error(f"a version is three numbers, as 0.8.0: not {args.version}")

    check_source(args.version)
    commit = git("rev-parse", "HEAD")
    if not args.archives_only:
        check_pushed(commit)

    players = []
    for platform in args.platforms:
        archive, symbols = build(platform, commit)
        named = RELEASES / f"Weltall-{args.version}-{platform}.zip"
        archive.replace(named)
        if symbols.is_file():
            symbols.replace(RELEASES / f"Weltall-{args.version}-{platform}-symbols.zip")
        players.append(named)
        print(f"release: {named.relative_to(REPO)}")
    notes = RELEASES / f"Weltall-{args.version}-notes.md"
    notes.write_text(unwrapped(NOTES.format(version=args.version)), encoding="utf-8")
    print(f"release: {notes.relative_to(REPO)}")
    if args.archives_only:
        return 0

    drafted = subprocess.run(["gh", "release", "create", f"v{args.version}", *map(str, players), "--draft", "--target", commit,
                              "--title", f"Weltall {args.version}", "--notes-file", str(notes)], cwd=REPO)
    if drafted.returncode != 0:
        refuse(f"gh could not make the draft (exit status {drafted.returncode})")
    git("tag", f"v{args.version}", commit)
    print(f"release: drafted v{args.version}: read it on the release page, then publish it there")
    return 0


if __name__ == "__main__":
    sys.exit(main())
