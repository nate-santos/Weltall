"""Check/apply the UE 5.8 Android clock and Shipping exec compatibility corrections.

Only an explicit --apply writes to the selected engine source checkout.
"""

import argparse
import json
from pathlib import Path


SOURCE = Path("Engine/Source/Runtime/Core/Private/Android/AndroidPlatformMisc.cpp")
HEADER = Path("Engine/Source/Runtime/Core/Public/Misc/AppTime.h")
OLD = b"CurrentVolume.TimeOfChange = FApp::GetCurrentTime();"
NEW = b"CurrentVolume.TimeOfChange = FAppTime::GetApproximateCurrentTime();"
EXEC_SOURCE = Path("Engine/Source/Runtime/Launch/Private/Android/AndroidJNI.cpp")
EXEC_START = b"class FAndroidEmbeddedExec : public FSelfRegisteringExec"
EXEC_END = b"} GAndroidEmbeddedExec;"
EXEC_GUARD = b"#if UE_ALLOW_EXEC_COMMANDS"


def check_version(root):
    version = json.loads((Path(root) / "Engine/Build/Build.version").read_text(encoding="utf-8-sig"))
    if (version.get("MajorVersion"), version.get("MinorVersion")) != (5, 8):
        raise ValueError("This correction supports UE 5.8 source checkouts only")


def needs_volume_time_fix(root):
    source = Path(root) / SOURCE
    return source.is_file() and OLD in source.read_bytes()


def fix_volume_time(root, apply=False):
    root = Path(root).resolve()
    check_version(root)
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


def needs_exec_commands_fix(root):
    source = Path(root) / EXEC_SOURCE
    if not source.is_file():
        return False
    data = source.read_bytes().replace(b"\r\n", b"\n")
    return EXEC_START in data and EXEC_GUARD + b"\n" + EXEC_START not in data


def fix_exec_commands(root, apply=False):
    check_version(root)
    source = Path(root) / EXEC_SOURCE
    data = source.read_bytes()
    newline = b"\r\n" if b"\r\n" in data else b"\n"
    start = EXEC_GUARD + newline + EXEC_START
    end = EXEC_END + newline + b"#endif // UE_ALLOW_EXEC_COMMANDS"
    if data.count(start) == 1 and data.count(end) == 1:
        return "already corrected"
    if (data.count(EXEC_START) != 1 or data.count(EXEC_END) != 1
            or data.index(EXEC_START) > data.index(EXEC_END)
            or start in data or end in data):
        raise ValueError("Unrecognized Android embedded exec; inspect the engine source before changing it")
    if not apply:
        return "correction required (run again with --apply)"
    # FExec::Exec is final when commands are disabled. Do not register this optional
    # Android command handler in that build; other configurations retain it verbatim.
    source.write_bytes(data.replace(EXEC_START, start, 1).replace(EXEC_END, end, 1))
    return "corrected; rebuild the Android runtime"


def main():
    from android_package import setting

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ue-root", type=Path, default=setting("UE_ROOT"))
    parser.add_argument("--apply", action="store_true", help="write the bounded corrections into the engine checkout")
    args = parser.parse_args()
    if not args.ue_root:
        parser.error("Set UE_ROOT or pass --ue-root")
    try:
        statuses = [("volume callback", fix_volume_time(args.ue_root, args.apply)),
                    ("Shipping exec", fix_exec_commands(args.ue_root, args.apply))]
    except (OSError, ValueError) as error:
        parser.exit(1, f"android_engine_fix: {error}\n")
    for label, status in statuses:
        print(f"{label}: {status}")
    return int(any(status.startswith("correction required") for _, status in statuses))


if __name__ == "__main__":
    raise SystemExit(main())
