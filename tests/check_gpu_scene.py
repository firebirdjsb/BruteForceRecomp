"""Compare real CPU/NV2A GPU draw output from a public synthetic fixture."""
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    executable, xbe, output = map(Path, sys.argv[1:4])
    hardware = '--hardware' in sys.argv[4:]
    d3d12 = '--d3d12' in sys.argv[4:]
    output.mkdir(parents=True, exist_ok=True)
    environment = {k: v for k, v in os.environ.items()
                   if not k.startswith(('RECOMP_', 'BF_'))}
    images = {}
    for mode in ('cpu', 'gpu', 'cpu-vsh', 'gpu-cpuvsh', 'gpu-vsh','cpu-surface','gpu-surface','cpu-hud','gpu-hud','gpu-async','gpu-vsh-async','gpu-defaultasync','gpu-mips'):
        image = output / (mode + '.raw')
        selected = mode + '-hardware' if hardware and mode.startswith('gpu') else mode
        if d3d12 and mode.startswith('gpu'):selected+='-d3d12'
        run = subprocess.run([str(executable), str(xbe), selected, str(image)],
                             env=environment, capture_output=True, text=True, timeout=120)
        (output / (mode + '.log')).write_text(run.stdout + run.stderr)
        if run.returncode:
            raise RuntimeError(f'{mode}: exit {run.returncode}\n{run.stderr[-3000:]}')
        data = image.read_bytes()
        if len(data) != 64 * 64 * 4:
            raise RuntimeError(f'{mode}: incomplete readback')
        images[mode] = data
        if mode == 'gpu-vsh':
            verified = re.findall(r'\[VSH-VERIFY\] (\d+) batches checked, (\d+) differ', run.stderr)
            if not verified or any(int(bad) for _, bad in verified):
                raise RuntimeError('GPU vertex program verification failed or did not run')
    if images['cpu-surface']!=images['gpu-surface']:
        raise RuntimeError('CPU-written movie surface differs after GPU presentation')
    if any(abs(a-b)>2 for a,b in zip(images['cpu-hud'],images['gpu-hud'])):
        raise RuntimeError('Zero-depth HUD differs between CPU and GPU rendering')
    for reference, candidate in (('cpu', 'gpu'), ('cpu-vsh', 'gpu-cpuvsh'), ('cpu-vsh', 'gpu-vsh'),('cpu','gpu-async'),('cpu-vsh','gpu-vsh-async'),('cpu','gpu-defaultasync')):
        a, b = images[reference], images[candidate]
        differences = []
        for i in range(64 * 64):
            if max(abs(x-y) for x, y in zip(a[4*i:4*i+3], b[4*i:4*i+3])) > 2:
                differences.append((i % 64, i // 64))
        # Known CPU inclusive-edge versus D3D top-left fill-rule differences.
        if len(differences) > 64 or any(not (x+y == 85 or 6 <= x <= 22 and 42 <= y <= 62)
                                        for x, y in differences):
            raise RuntimeError(f'{reference}/{candidate}: unexpected differing pixels {differences[:80]}')
        print(f'{reference}/{candidate}: {len(differences)} known edge pixels; other RGB within 2/255')


if __name__ == '__main__':
    main()
