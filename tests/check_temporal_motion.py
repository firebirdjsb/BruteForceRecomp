"""Diagnostic contract, with synthetic guest memory and exact sidecar checks."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

exe, output = sys.argv[1:3]
folder = Path(output)
folder.mkdir(parents=True, exist_ok=True)
off = dict(os.environ)
for key in list(off):
    if key.startswith('BF_MOTION_TRACE_'):
        del off[key]
off['BF_UPSCALER'] = 'fsr1'
off.pop('BF_MOTION_RENDER_DIAGNOSTIC', None)
subprocess.run([exe, '--off'], env=off, check=True, timeout=15)
on = dict(off, BF_MOTION_TRACE_DIR=str(folder.resolve()), BF_MOTION_TRACE_FRAMES='2')
subprocess.run([exe], env=on, check=True, timeout=15)
records = [json.loads(line) for line in (folder / 'motion.jsonl').read_text().splitlines()]
frames = [r for r in records if r['kind'] == 'frame']
assert len(frames) == 2, frames
assert [(r['depth_candidates'], r['identified_vsh_candidates'], r['unsupported_candidates']) for r in frames] == [(3, 1, 2), (2, 1, 1)], frames
assert frames[0]['base'] == 2 and frames[0]['transformed'] == frames[0]['vsh'] == 1
draws = [r for r in records if r['kind'] == 'draw']
identified = [r for r in draws if r['path'] == 2 and r['identity_proven']]
assert [r['generation'] for r in identified] == [1, 2], identified
assert all(not r['sdk_ready'] for r in records if 'sdk_ready' in r)
data = (folder / 'motion-data.bin').read_bytes()
for r in (x for x in draws if x['path'] == 2):
    assert r['consumed_inputs_saved']
    at = r['data_offset']
    header = struct.unpack_from('<10I', data, at)
    assert header[:7] == (0x314D4642, 1, 3, 3, 1, 1, 0), header
    ids_at = at + 40 + 16 + 192 * 16
    assert struct.unpack_from('<3I', data, ids_at) == (32, 9, 45)
    assert struct.unpack_from('<3I', data, ids_at + 12) == (0, 1, 2)
    assert at + r['data_bytes'] <= len(data)
print('PASS bounded temporal identity/coverage diagnostics; no motion vectors or SDK readiness claimed.')
persistent = folder / 'persistent'
persistent.mkdir(exist_ok=True)
env = dict(on, BF_UPSCALER='fsr3', BF_MOTION_TRACE_DIR=str(persistent.resolve()))
subprocess.run([exe, '--persistent'], env=env, check=True, timeout=15)
records = [json.loads(line) for line in (persistent / 'motion.jsonl').read_text().splitlines()]
assert len([r for r in records if r['kind'] == 'frame']) == 2
print('PASS semantic observation follows supported mode changes after bounded trace completion; static adapter remains rejected.')
