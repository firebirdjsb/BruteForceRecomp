"""Exercise opt-in texture timing with public NV2A frames and real intervals."""
import math
import os
from pathlib import Path
import re
import subprocess
import sys


SUMMARY = re.compile(
    r'\[GPU-PERF\] texture lookup/validation/upload ([\d.]+) ms/s across (\d+) calls; '
    r'interval ([\d.]+) s; lookup hints (\d+), searches (\d+); total (\d+) ns; '
    r'requested source spans (\d+) bytes')
PHASE = re.compile(
    r'\[GPU-PERF\] texture phase (\w+): (\d+) ns across (\d+) calls; '
    r'([\d.]+) ns/call; ([\d.]+) ms/s')
NAMES = ('alias_sync', 'descriptor_search', 'exact_validation', 'decode_upload_prepare')


def expected_frames():
    first = b''.join(
        (0xFFE03020 if x < 16 else 0xFF20C060 if x < 24 else
         0xFFE03020 if x < 32 else 0xFF20C060 if x < 40 else
         0xFF3050D0).to_bytes(4, 'little')
        for _ in range(64) for x in range(64))
    return first + (0xFFE0B030).to_bytes(4, 'little') * (64 * 64)


def run(executable, xbe, output, enabled, d3d12):
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('BF_', 'RECOMP_'))}
    if enabled:
        environment['BF_PROFILE_RENDER'] = '1'
    mode = 'gpu-texprofile' + ('' if enabled else '-short')
    if d3d12:
        mode += '-d3d12'
    destination = output / ('enabled.raw' if enabled else 'disabled.raw')
    result = subprocess.run([str(executable), str(xbe), mode, str(destination)],
                            env=environment, capture_output=True, text=True, timeout=90)
    (output / ('enabled.log' if enabled else 'disabled.log')).write_text(
        result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'{mode}: exit {result.returncode}\n{result.stderr[-4000:]}')
    pixels = destination.read_bytes()
    if pixels != expected_frames():
        raise RuntimeError(f'{mode}: public expected source/palette/alias pixels changed')
    return result.stderr, pixels


def check_windows(log):
    windows = []
    for line in log.splitlines():
        summary = SUMMARY.fullmatch(line)
        if summary:
            rate, calls, seconds, hints, searches, total, requested = summary.groups()
            windows.append({'rate': float(rate), 'calls': int(calls), 'seconds': float(seconds),
                            'hints': int(hints), 'searches': int(searches), 'total': int(total),
                            'requested': int(requested), 'phases': {}})
        phase = PHASE.fullmatch(line)
        if phase:
            if not windows:
                raise RuntimeError('phase report precedes its whole-call report')
            name, ns, calls, average, rate = phase.groups()
            phases = windows[-1]['phases']
            if name in phases:
                raise RuntimeError(f'duplicate phase report: {name}')
            phases[name] = (int(ns), int(calls), float(average), float(rate))
    if len(windows) != 2:
        raise RuntimeError(f'expected two real ten-second reports, got {len(windows)}')
    for index, window in enumerate(windows):
        if not 10 <= window['seconds'] <= 30:
            raise RuntimeError(f'window {index}: unexpected reporting interval')
        if tuple(window['phases']) != NAMES:
            raise RuntimeError(f'window {index}: missing/reordered bounded phases')
        expected_calls = (6, 5, 5, 5) if index == 0 else (2, 0, 0, 0)
        if window['calls'] != expected_calls[0]:
            raise RuntimeError(f'window {index}: whole-call count did not reset')
        if (window['hints'], window['searches'], window['requested']) != (
                (3, 2, 576) if index == 0 else (0, 0, 0)):
            raise RuntimeError(f'window {index}: lookup/requested-span counters changed or failed to reset')
        accounted = 0
        for name, expected_calls_in_phase in zip(NAMES, expected_calls):
            ns, calls, average, rate = window['phases'][name]
            if calls != expected_calls_in_phase or (not calls and ns != 0):
                raise RuntimeError(f'window {index}/{name}: phase count/time did not reset')
            if calls and ns <= 0:
                raise RuntimeError(f'window {index}/{name}: exercised phase has no measured time')
            expected_average = ns / calls if calls else 0
            if not math.isclose(average, expected_average, abs_tol=1.1):
                raise RuntimeError(f'window {index}/{name}: inconsistent ns/call')
            if not math.isclose(rate, ns / 1_000_000 / window['seconds'], abs_tol=0.02):
                raise RuntimeError(f'window {index}/{name}: inconsistent ms/s')
            accounted += ns
        # Each line rounds QPC-derived nanoseconds independently. All four
        # exclusive buckets must still account for the outer elapsed interval.
        if abs(accounted - window['total']) > 4:
            raise RuntimeError(f'window {index}: exclusive phases do not partition whole-call time')
        if not math.isclose(window['rate'], window['total'] / 1_000_000 / window['seconds'], abs_tol=0.02):
            raise RuntimeError(f'window {index}: whole ms/s and nanoseconds disagree')


def main():
    executable, xbe, output = (Path(value).resolve() for value in sys.argv[1:4])
    d3d12 = '--d3d12' in sys.argv[4:]
    output.mkdir(parents=True, exist_ok=True)
    # Both processes exercise a real statistics boundary; the disabled process
    # skips only the fixture's second sleep, leaving all draw/input code intact.
    enabled_log, enabled_pixels = run(executable, xbe, output, True, d3d12)
    disabled_log, disabled_pixels = run(executable, xbe, output, False, d3d12)
    check_windows(enabled_log)
    if '[GPU-PERF]' in disabled_log:
        raise RuntimeError('profiling-disabled process emitted opt-in timing output')
    if '[GPU-D3D11]' not in disabled_log or 'source bytes compared' not in disabled_log:
        raise RuntimeError('profiling-disabled fixture did not exercise its statistics interval')
    if enabled_pixels != disabled_pixels:
        raise RuntimeError('profiling changed output pixels')
    print('Texture profiling: two exclusive/reset ten-second windows; correct counts/units; '
          'profile-off silence; exact public pixels unchanged.')


if __name__ == '__main__':
    main()
