"""Validate the opt-in owner diagnostic without a retail binary or GPU."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='bf-hud-owner-') as folder:
    root = Path(folder)
    path, trigger = root / 'owners.jsonl', root / 'armed'
    clean = {k: v for k, v in os.environ.items() if not k.startswith('BF_HUD_OWNER_')}
    subprocess.run([exe, '--trace-disabled'], env=clean, check=True)
    env = dict(clean, BF_HUD_OWNER_TRACE=str(path), BF_HUD_OWNER_TRIGGER=str(trigger), BF_HUD_OWNER_LIMIT='5')
    subprocess.run([exe, '--trace-disabled'], env=env, check=True)
    assert not path.exists()
    trigger.write_text('synthetic diagnostic armed')
    subprocess.run([exe, '--trace'], env=env, check=True)
    records = [json.loads(line) for line in path.read_text().splitlines()]
    assert len(records) == 5
    owner, sprite, rect, end_sprite, end_owner = records
    assert (owner['stage'], owner['draw'], owner['uid'], owner['rect'], owner['owners']) == (
        4, 0xC6AE0, 0xAABBCCDD, [50, 300, 80, 100], [])
    assert owner['return'] == 0xB2761 and sprite['return'] == 0x123456
    assert sprite['owners'] == [[owner['token'], 0x20000, 0x30000, 0xC6AE0]]
    assert (rect['token'], rect['x'], rect['width'], rect['anchor']) == (sprite['token'], 50, 100, 0)
    assert end_sprite['token'] == sprite['token'] and end_owner['token'] == owner['token']
print('HUD owner trace default-off, host trigger, five-line budget, exact fields and nested ownership passed.')
