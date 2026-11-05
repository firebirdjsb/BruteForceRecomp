"""Actual backend pixel proof for the clear/motion b2 register collision."""
import os
from pathlib import Path
import subprocess
import sys

executable, xbe, output = map(Path, sys.argv[1:4])
output.mkdir(parents=True, exist_ok=True)
environment = {key: value for key, value in os.environ.items()
               if not key.startswith(('RECOMP_', 'BF_'))}
for api in ('d3d11', 'd3d12'):
    for path in ('base', 'xdraw', 'vsh'):
        mode = f'gpu-motion-off-{path}-hardware-{api}'
        target = output / (mode + '.raw')
        result = subprocess.run([str(executable), str(xbe), mode, str(target)],
                                env=environment, capture_output=True, text=True, timeout=60)
        (output / (mode + '.log')).write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stderr[-3000:]
        pixels = target.read_bytes()
        assert len(pixels) == 64 * 64 * 4
        for y in range(64):
            for x in range(64):
                expected = (0xFF00FF00 if x < 4 and y < 4 else
                            0xFF2674A1 if 10 <= x < 20 and 10 <= y < 20 else 0xFF102030)
                actual = int.from_bytes(pixels[4 * (64 * y + x):4 * (64 * y + x + 1)], 'little')
                assert actual == expected, f'{mode} ({x},{y}) {actual:08x} != {expected:08x}'
        print(f'{api} {path}: all 4096 RT0 pixels exact after nonblack partial clear')
