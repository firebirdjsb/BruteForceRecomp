"""Verify bounded pre-overwrite probes independently of validated scene export."""
from pathlib import Path
import json
import os
import struct
import subprocess
import sys
import tempfile


def check_pair(path, width=64, height=64, stage=None):
    info = json.loads(path.read_text())
    assert (info['width'], info['height'], info['depth_draws']) == (width, height, 1), info
    assert info['before_native_ui'] is False and info['temporal_sdk_ready'] is False
    assert info['motion_available'] is False and info['jitter_available'] is False
    assert info['capture_stage'] in ('before_depth_clear', 'shared_depth_write', 'depth_recreate')
    if stage:
        assert info['capture_stage'] == stage, info
    depth = struct.unpack(f'<{width*height}f', path.with_suffix('.depth.f32').read_bytes())
    left, right = height // 2 * width + width // 4, height // 2 * width + 3 * width // 4
    assert abs(depth[left] - .75) < 1e-6 and abs(depth[right] - .25) < 1e-6
    header = f'P6\n{width} {height}\n255\n'.encode()
    image = path.with_suffix('.color.ppm').read_bytes()
    assert image.startswith(header)
    rgb = image[len(header):]
    assert len(rgb) == width * height * 3
    assert rgb[left*3:left*3+3] == b'\0\0\xff' and rgb[right*3:right*3+3] == b'\xff\0\0'


def launch(arguments, env, case):
    result = subprocess.run([str(p) for p in arguments], env=env,
                            capture_output=True, text=True, timeout=60)
    (case / 'fixture.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, (result.returncode, result.stderr[-3000:])


def main():
    scene, direct, xbe, out = (Path(p).resolve() for p in sys.argv[1:5])
    api = sys.argv[5] if len(sys.argv) > 5 else 'd3d11'
    out.mkdir(parents=True, exist_ok=True)
    clean = {k: v for k, v in os.environ.items() if not k.startswith(('BF_', 'RECOMP_'))}
    stages = {'depth-full': 'before_depth_clear', 'depth-partial': 'before_depth_clear',
              'shared-depth-full': 'before_depth_clear', 'shared-depth-draw': 'shared_depth_write',
              'unconditional-write': 'shared_depth_write', 'depth-recreate': 'depth_recreate'}
    cases = [(name, stage, True, {}) for name, stage in stages.items()]
    cases += [('off', None, False, {}), ('minimum-default', None, True, {'BF_DEPTH_PROBE_MIN_DRAWS': None}),
              ('zero-budget', None, True, {'BF_DEPTH_PROBE_COUNT': '0'}),
              ('trigger-absent', None, True, {'trigger': False}),
              ('trigger-present', 'before_depth_clear', True, {'trigger': True})]
    for name, expected_stage, enabled, overrides in cases:
        case = Path(tempfile.mkdtemp(prefix=name + '-', dir=out))
        env = dict(clean, BF_SCENE_CAPTURE_DIR=str(case / 'validated'), BF_SCENE_CAPTURE_COUNT='1',
                   BF_SCENE_CAPTURE_MIN_DEPTH_DRAWS='1')
        if enabled:
            env.update(BF_DEPTH_PROBE_DIR=str(case / 'probe'), BF_DEPTH_PROBE_MIN_DRAWS='1')
        for key, value in overrides.items():
            if key == 'trigger':
                trigger = case / 'armed'
                env['BF_DEPTH_PROBE_TRIGGER'] = str(trigger)
                if value:
                    trigger.write_text('synthetic probe armed')
            elif value is None:
                env.pop(key, None)
            else:
                env[key] = value
        fixture = name if name in stages else 'depth-full'
        mode = 'gpu-snapshot-' + fixture + ('-d3d12' if api == 'd3d12' else '')
        launch((scene, xbe, mode, case / 'final.raw'), env, case)
        assert not list((case / 'validated').glob('frame-*.json')), 'Probe bypassed validated export guard'
        probes = list((case / 'probe').glob('probe-*.json'))
        assert len(probes) == int(expected_stage is not None), (name, probes)
        for path in probes:
            check_pair(path, stage=expected_stage)
        final = (case / 'final.raw').read_bytes()
        assert len(final) == 64*64*4 and all(p == 0xff00ff00 for p in struct.unpack('<4096I', final))
        print(f'{api}/{name}: diagnostic stage/budget/trigger and unchanged final UI checked')
    # Real D3D API tests retain a normal consumer's resources while invoking
    # distinct probe stages, duplicates and later writes. Default budget=4;
    # explicitly requesting 8 exercises the hard upper bound.
    for count in (None, '8'):
        case = Path(tempfile.mkdtemp(prefix='independent-' + (count or 'default') + '-', dir=out))
        env = dict(clean, BF_DEPTH_PROBE_DIR=str(case / 'probe'), BF_DEPTH_PROBE_MIN_DRAWS='1')
        if count:
            env['BF_DEPTH_PROBE_COUNT'] = count
        arguments = [direct, '--probe'] + (['--d3d12'] if api == 'd3d12' else [])
        launch(arguments, env, case)
        paths = list((case / 'probe').glob('probe-*.json'))
        assert len(paths) == (8 if count else 4), paths
        for path in paths:
            check_pair(path, height=48)
        first_frame = [json.loads(p.read_text()) for p in paths if json.loads(p.read_text())['frame'] == 91]
        assert {p['capture_stage'] for p in first_frame} == {'before_depth_clear', 'shared_depth_write'}
        print(f'{api}/independent-{count or "default"}: separate normal resources, dedupe and bounded captures checked')


if __name__ == '__main__':
    main()
