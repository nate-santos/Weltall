"""Record live Android HAL temperatures during an explicitly described workload.

    python Scripts/android_thermal.py --seconds 600 --label disc2-default

Does not launch/restart the game or change settings/volume. Raw readings and
hashes remain under ignored out/. Exits nonzero on incomplete observation or
an exceeded limit; --home-on-limit optionally backgrounds the game on a limit.
Foreground presence alone does not establish active gameplay: review the scene.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import time
import uuid

from android_game_reference import Device, PACKAGE
from android_test import choose_device, default_adb

REPO = Path(__file__).resolve().parent.parent


def parse_thermal(text):
    """Use current HAL values only: cached CPU temperatures can be stale."""
    status = re.search(r"^Thermal Status:\s*(\d+)\s*$", text, re.M)
    current = text.split("Current temperatures from HAL:", 1)
    if status is None or len(current) != 2:
        raise ValueError("missing thermal status or live HAL temperature section")
    section = current[1].split("Current cooling devices from HAL:", 1)[0]
    readings = []
    for value, kind, name, severity in re.findall(
        r"Temperature\{mValue=([^,]+), mType=(\d+), mName=([^,]+), mStatus=(\d+)\}", section
    ):
        number = float(value)
        if not math.isfinite(number):
            continue
        readings.append(dict(name=name, type=int(kind), celsius=number, status=int(severity)))
    # Types 0..3 represent CPU, GPU, battery and skin. Other values include
    # currents, not temperatures; never include these in temperature ranges.
    temperatures = [r for r in readings if r['type'] in (0, 1, 2, 3)]
    if not temperatures:
        raise ValueError("no live CPU/GPU/battery/skin readings")
    return dict(status=int(status[1]), temperatures=temperatures)


def parse_battery(text):
    fields = {}
    for key in ('temperature', 'level', 'status', 'AC powered', 'USB powered', 'Wireless powered'):
        found = re.search(r'^\s*' + re.escape(key) + r':\s*(\S+)', text, re.M)
        if found:
            fields[key] = found[1]
    if 'temperature' not in fields:
        raise ValueError('battery temperature missing')
    fields['celsius'] = int(fields['temperature']) / 10
    return fields


def limit_reason(thermal, skin_limit):
    if thermal['status'] >= 2:
        return 'Android thermal status reached MODERATE or higher'
    if any(t['status'] >= 2 for t in thermal['temperatures']):
        return 'A live temperature sensor reached MODERATE or higher'
    if any(t['type'] == 3 and t['celsius'] >= skin_limit for t in thermal['temperatures']):
        return 'Configured skin-temperature observation limit reached'
    return None


def summarize(samples):
    result = {'samples': len(samples)}
    for kind, label in [(0, 'cpu'), (1, 'gpu'), (2, 'battery'), (3, 'skin')]:
        values = [t['celsius'] for s in samples for t in s['thermal']['temperatures'] if t['type'] == kind]
        if values:
            result[label + '_celsius_range'] = [min(values), max(values)]
    if samples:
        result['elapsed_seconds'] = samples[-1]['elapsed_seconds']
        result['max_thermal_status'] = max(s['thermal']['status'] for s in samples)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--adb', type=Path, default=default_adb())
    p.add_argument('--serial')
    p.add_argument('--seconds', type=int, default=600)
    p.add_argument('--interval', type=int, default=15)
    p.add_argument('--label', required=True, help='Describe the workload/settings; observer does not supply input')
    p.add_argument('--skin-limit', type=float, default=38.0, help='Observation limit, not a universal safety rating')
    p.add_argument('--home-on-limit', action='store_true')
    args = p.parse_args()
    if not (15 <= args.seconds <= 3600 and 5 <= args.interval <= 60 and args.seconds % args.interval == 0):
        p.error('use 15–3600 seconds, interval 5–60, and a whole number of intervals')
    if not math.isfinite(args.skin_limit) or not 30 <= args.skin_limit <= 43:
        p.error('skin limit must be 30–43 Celsius')
    if not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9-]{0,63}', args.label):
        p.error('label must be 1–64 letters, digits or hyphens')
    if not args.adb:
        p.error('specify --adb or install platform-tools')
    import subprocess
    serial = choose_device(subprocess.check_output([str(args.adb), 'devices'], text=True), args.serial)
    run_id = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8] + '-' + args.label
    folder = REPO / 'out/testing/thermal' / run_id
    folder.mkdir(parents=True, exist_ok=False)
    request = dict(vars(args), serial=serial, started_utc=datetime.now(timezone.utc).isoformat())
    (folder / 'request.json').write_text(json.dumps(request, default=str, indent=2), encoding='utf-8')
    print(json.dumps({'folder': str(folder)}), flush=True)
    samples, before, error, reason = [], None, None, None
    started = time.monotonic()
    with (folder / 'transport.log').open('w', encoding='utf-8') as log:
        d = Device(args.adb, serial, log)
        try:
            before = d.player_snapshot()
            (folder / 'player-before.json').write_text(json.dumps(before, indent=2), encoding='utf-8')
            settings = d.read('files/Weltall/Settings/Settings.txt').stdout
            (folder / 'player-settings.txt').write_bytes(settings)
            request['settings_sha256'] = hashlib.sha256(settings).hexdigest()
            for key, prop in [('model', 'ro.product.model'), ('android', 'ro.build.version.release'),
                              ('build', 'ro.build.fingerprint')]:
                request[key] = d.run(['shell', 'getprop', prop]).stdout.decode().strip()
            (folder / 'package.txt').write_bytes(d.run(['shell', 'dumpsys', 'package', PACKAGE]).stdout)
            (folder / 'request.json').write_text(json.dumps(request, default=str, indent=2), encoding='utf-8')
            pids = d.run(['shell', 'pidof', PACKAGE]).stdout.strip()
            if not re.fullmatch(rb'\d+(?:\s+\d+)*', pids):
                raise ValueError('game process missing')
            for index in range(args.seconds // args.interval + 1):
                delay = started + index * args.interval - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                raw = {}
                for key, command in [('thermal', ['shell', 'dumpsys', 'thermalservice']),
                                     ('battery', ['shell', 'dumpsys', 'battery']),
                                     ('window', ['shell', 'dumpsys', 'window'])]:
                    raw[key] = d.run(command).stdout.decode(errors='replace')
                    (folder / f'{index:03}-{key}.txt').write_text(raw[key], encoding='utf-8')
                focus = [s.strip() for s in raw['window'].splitlines() if 'mCurrentFocus=' in s]
                same_process = d.run(['shell', 'pidof', PACKAGE], check=False).stdout.strip() == pids
                sample = dict(index=index, utc=datetime.now(timezone.utc).isoformat(), elapsed_seconds=time.monotonic() - started,
                              thermal=parse_thermal(raw['thermal']), battery=parse_battery(raw['battery']),
                              focus=focus, same_process=same_process)
                samples.append(sample)
                (folder / 'samples.json').write_text(json.dumps(samples, indent=2), encoding='utf-8')
                print(json.dumps({'sample': index, **summarize([sample])}), flush=True)
                reason = limit_reason(sample['thermal'], args.skin_limit)
                if reason:
                    if args.home_on_limit and any(PACKAGE + '/' in s for s in focus):
                        d.run(['shell', 'input', 'keyevent', 'KEYCODE_HOME'])
                    break
                if not same_process or not any(PACKAGE + '/' in s for s in focus):
                    raise ValueError('game process changed or foreground was lost; workload observation incomplete')
        except (ValueError, RuntimeError, OSError, subprocess.SubprocessError, KeyboardInterrupt) as exc:
            error = str(exc) or type(exc).__name__
        finally:
            try:
                after = d.player_snapshot()
                (folder / 'player-after.json').write_text(json.dumps(after, indent=2), encoding='utf-8')
                retained = before == after if before is not None else False
            except (RuntimeError, OSError, subprocess.SubprocessError):
                retained = False
            summary = dict(**summarize(samples), error=error, limit_reason=reason,
                           normal_player_files_unchanged=retained,
                           complete=not error and not reason and len(samples) == args.seconds // args.interval + 1,
                           scope='Foreground observation only; scene activity and performance require separate evidence')
            (folder / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
            print(json.dumps(summary), flush=True)
    return int(not summary['complete'] or not retained)


if __name__ == '__main__':
    raise SystemExit(main())
