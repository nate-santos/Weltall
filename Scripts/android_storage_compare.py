"""Bounded same-APK extracted/packaged disc comparison using normal isolated save loads.

All saves, actions, CSVs and reports must remain local. This does not change the
default reader or reclaim disc copies; gameplay images/transitions need review.
"""
import argparse
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys
import time
import uuid

from android_game_reference import Device, PACKAGE, COMMAND_FILE
from android_package import REPO
from android_test import choose_device, default_adb
from android_thermal import parse_thermal, limit_reason


def analyze(path):
    columns = ['FrameTime', 'GameThreadTime', 'RenderThreadTime', 'RHIThreadTime', 'GPUTime']
    frames = []
    io = {}
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.reader(stream)
        header = next(reader)
        indexes = {key: header.index(key) for key in columns}
        io_indexes = {key: i for i, key in enumerate(header) if 'WeltallDiscIO/' in key}
        for raw in reader:
            if not raw or raw == header or raw[0].startswith('['):
                continue
            try:
                frame = {key: float(raw[i]) for key, i in indexes.items()}
            except (ValueError, IndexError) as error:
                raise ValueError('unexpected non-numeric CSV frame row') from error
            if not all(math.isfinite(value) and value >= 0 for value in frame.values()) or frame['FrameTime'] <= 0:
                raise ValueError('invalid CSV frame metric')
            frames.append(frame)
            for key, i in io_indexes.items():
                value = float(raw[i])
                if not math.isfinite(value) or value < 0:
                    raise ValueError('invalid disc IO counter')
                io.setdefault(key, []).append(value)
    if not frames:
        raise ValueError('no numeric CSV frames')

    def distribution(values):
        values = sorted(values)
        return dict(median=statistics.median(values), p95=values[int((len(values)-1)*.95)],
                    p99=values[int((len(values)-1)*.99)], max=max(values), mean=statistics.mean(values))

    seconds = sum(frame['FrameTime'] for frame in frames) / 1000
    return dict(numeric_frames=len(frames), seconds=seconds, fps=len(frames)/seconds,
                metrics_ms={key: distribution([frame[key] for frame in frames]) for key in columns},
                frames_over_100ms=sum(frame['FrameTime'] > 100 for frame in frames),
                io={key: dict(total=sum(values), **distribution(values)) for key, values in io.items()},
                exclusions='No numeric frames excluded, including stalls and zero-GPU frames')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--save', type=Path, required=True)
    parser.add_argument('--actions', type=Path, required=True)
    parser.add_argument('--load-disc', type=int, choices=(1, 2), default=2)
    parser.add_argument('--seconds', type=int, default=100)
    parser.add_argument('--serial')
    parser.add_argument('--adb', default=default_adb())
    args = parser.parse_args()
    if not 96 <= args.seconds <= 180:
        parser.error('use 96–180 seconds: normal loading followed by at least 60 profiled seconds')
    serial = choose_device(subprocess.check_output([str(args.adb), 'devices'], text=True), args.serial)
    run_id = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8]
    folder = REPO / 'out/testing/storage' / ('comparison-' + run_id)
    folder.mkdir(parents=True, exist_ok=False)
    print(json.dumps({'folder': str(folder)}), flush=True)
    phases = []
    with (folder/'transport.log').open('w', encoding='utf-8') as log:
        device = Device(args.adb, serial, log)
        for index, reader in enumerate(('extracted', 'packaged', 'extracted', 'packaged')):
            phase = folder / str(index)
            phase.mkdir()
            command = [sys.executable, '-X', 'utf8', str(REPO/'Scripts/android_game_reference.py'),
                       '--save', str(args.save.resolve()), '--actions', str(args.actions.resolve()),
                       '--load-disc', str(args.load_disc), '--seconds', str(args.seconds), '--every', '0',
                       '--profile', '--disc-reader', reader, '--serial', serial, '--adb', str(args.adb),
                       '--label', 'storage-' + run_id + '-' + str(index), '--timeout', str(args.seconds*3+180)]
            samples = []
            stop = None
            with (phase/'host.txt').open('w', encoding='utf-8') as output:
                with subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT) as process:
                    next_sample = 0
                    try:
                        while process.poll() is None:
                            if time.monotonic() >= next_sample:
                                thermal = parse_thermal(device.run(['shell', 'dumpsys', 'thermalservice']).stdout.decode())
                                samples.append(thermal)
                                stop = stop or limit_reason(thermal, 38)
                                memory = device.run(['shell', 'dumpsys', 'meminfo', PACKAGE], check=False).stdout
                                (phase/('memory-' + str(len(samples)) + '.txt')).write_bytes(memory)
                                next_sample = time.monotonic() + 20
                            time.sleep(1)
                    except BaseException:
                        owned = device.run(['exec-out', 'cat', COMMAND_FILE], check=False).stdout
                        if run_id.encode() in owned:
                            device.run(['shell', 'am', 'force-stop', PACKAGE], check=False)
                            device.run(['shell', 'rm', '-f', COMMAND_FILE], check=False)
                        process.terminate()
                        raise
                    status = process.returncode
            lines = (phase/'host.txt').read_text(encoding='utf-8-sig').splitlines()
            reference = Path(json.loads(next(line for line in lines if line.startswith('{"folder":')))['folder'])
            summary = json.loads((reference/'summary.json').read_text())
            if status or not summary.get('automation_passed') or not summary.get('csv_capture_present'):
                raise RuntimeError('reference failed; inspect ' + str(phase/'host.txt'))
            if not summary.get('normal_player_files_unchanged') or not summary.get('launch_override_absent'):
                raise RuntimeError('reference retention/cleanup failed')
            metrics = analyze(reference/'device/profile.csv')
            metadata = json.loads((reference/'request.json').read_text())
            phases.append(dict(reader=reader, reference=str(reference), metrics=metrics, thermal=samples,
                               installed_apks=metadata['installed_apks_sha256']))
            (folder/'summary.json').write_text(json.dumps(dict(phases=phases, complete=False), indent=2))
            print(json.dumps(dict(phase=index, reader=reader, fps=metrics['fps'], reference=str(reference))), flush=True)
            if stop:
                raise RuntimeError(stop + '; remaining phases not started')
    if any(row['installed_apks'] != phases[0]['installed_apks'] for row in phases):
        raise RuntimeError('installed APK changed during comparison')
    result = dict(complete=True, phases=phases, gameplay_acceptance='Requires matching trace/image and performance review',
                  scope='Repeated short same-APK normal loads; system file cache not cleared; no cleanup or default changes')
    (folder/'summary.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(dict(complete=True, folder=str(folder))), flush=True)


if __name__ == '__main__':
    main()
