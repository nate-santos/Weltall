"""Check/apply the UE 5.8 Android volume-callback clock correction.

Only an explicit --apply writes to the selected engine source checkout.
"""

import argparse
import json
from pathlib import Path


SOURCE = Path("Engine/Source/Runtime/Core/Private/Android/AndroidPlatformMisc.cpp")
HEADER = Path("Engine/Source/Runtime/Core/Public/Misc/AppTime.h")
OLD = b"CurrentVolume.TimeOfChange = FApp::GetCurrentTime();"
NEW = b"CurrentVolume.TimeOfChange = FAppTime::GetApproximateCurrentTime();"


def needs_volume_time_fix(root):
    source = Path(root) / SOURCE
    return source.is_file() and OLD in source.read_bytes()


def fix_volume_time(root, apply=False):
    root = Path(root).resolve()
    version = json.loads((root / "Engine/Build/Build.version").read_text(encoding="utf-8-sig"))
    if (version.get("MajorVersion"), version.get("MinorVersion")) != (5, 8):
        raise ValueError("This correction supports UE 5.8 source checkouts only")
    source = root / SOURCE
    data = source.read_bytes()
    if data.count(NEW) == 1 and OLD not in data:
        return "already corrected"
    if data.count(OLD) != 1 or NEW in data:
        raise ValueError("Unrecognized volume callback; inspect the engine source before changing it")
    if b"GetApproximateCurrentTime()" not in (root / HEADER).read_bytes():
        raise ValueError("The engine does not provide the required thread-safe approximate clock")
    if not apply:
        return "correction required (run again with --apply)"
    # This timestamp is receiver metadata, not simulation or rendering time.
    # Keep the same clock domain without requiring a JNI thread to inherit game time.
    source.write_bytes(data.replace(OLD, NEW, 1))
    return "corrected; rebuild the Android runtime"


def main():
    from android_package import setting

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ue-root", type=Path, default=setting("UE_ROOT"))
    parser.add_argument("--apply", action="store_true", help="write the one-line correction into the engine checkout")
    args = parser.parse_args()
    if not args.ue_root:
        parser.error("Set UE_ROOT or pass --ue-root")
    try:
        status = fix_volume_time(args.ue_root, args.apply)
    except (OSError, ValueError) as error:
        parser.exit(1, f"android_engine_fix: {error}\n")
    print(status)
    return int(status.startswith("correction required"))


if __name__ == "__main__":
    raise SystemExit(main())
