"""Check ordered NV2A scene/depth provenance and exclusion of later UI draws."""
from pathlib import Path
import json
import os
import struct
import subprocess
import sys
import tempfile


def check_case(exe, xbe, out, api, name, expected_capture):
    # Retain each bounded synthetic run as evidence, avoiding stale files from
    # an earlier red regression being mistaken for this process's captures.
    case = Path(tempfile.mkdtemp(prefix=name + '-', dir=out))
    env = {k: v for k, v in os.environ.items() if not k.startswith(('BF_', 'RECOMP_'))}
    env.update(BF_SCENE_CAPTURE_DIR=str(case.resolve()), BF_SCENE_CAPTURE_COUNT='1',
               BF_SCENE_CAPTURE_MIN_DEPTH_DRAWS='1')
    mode = 'gpu-snapshot-' + name + ('-d3d12' if api == 'd3d12' else '')
    result = subprocess.run([str(exe), str(xbe), mode, str(case / 'final.raw')],
                            env=env, capture_output=True, text=True, timeout=60)
    (case / 'fixture.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, (result.returncode, result.stderr[-3000:])
    final = (case / 'final.raw').read_bytes()
    assert len(final) == 64 * 64 * 4 and all(p == 0xff00ff00 for p in struct.unpack('<4096I', final))
    metadata = list(case.glob('frame-*.json'))
    assert len(metadata) == int(expected_capture), f'{name}: expected {int(expected_capture)} captures, got {metadata}'
    if not expected_capture:
        return
    path = metadata[0]
    info = json.loads(path.read_text())
    assert info['width'] == 64 and info['height'] == 64 and info['depth_draws'] == 1, info
    assert info['before_native_ui'] and not info['temporal_sdk_ready']
    depth = struct.unpack('<4096f', path.with_suffix('.depth.f32').read_bytes())
    assert abs(depth[32 * 64 + 16] - .75) < 1e-6 and abs(depth[32 * 64 + 48] - .25) < 1e-6
    raw = path.with_suffix('.color.ppm').read_bytes()
    head = b'P6\n64 64\n255\n'
    assert raw.startswith(head)
    rgb = raw[len(head):]
    assert len(rgb) == 64 * 64 * 3
    assert rgb[(32 * 64 + 16) * 3:(32 * 64 + 16) * 3 + 3] == b'\0\0\xff'
    assert rgb[(32 * 64 + 48) * 3:(32 * 64 + 48) * 3 + 3] == b'\xff\0\0'


def main():
    exe, xbe, out = (Path(p).resolve() for p in sys.argv[1:4])
    api = sys.argv[4] if len(sys.argv) > 4 else 'd3d11'
    out.mkdir(parents=True, exist_ok=True)
    cases = ((name, True) for name in ('normal', 'before-world', 'rebuild',
                                      'stencil-only', 'shared-stencil-only',
                                      'stencil-draw', 'shared-stencil-draw'))
    rejected = ((name, False) for name in (
        'depth-full', 'depth-partial', 'shared-depth-full', 'shared-depth-partial',
        'redraw', 'shared-redraw', 'nextframe', 'color-only', 'depth-recreate',
        'shared-depth-draw', 'attachment-switch', 'unconditional-write'))
    failures = []
    for name, expected in (*cases, *rejected):
        try:
            check_case(exe, xbe, out, api, name, expected)
            print(f'{api}/{name}: {"matching scene exported" if expected else "stale provenance rejected"}')
        except AssertionError as error:
            failures.append(f'{name}: {error}')
    if failures:
        raise AssertionError('\n'.join(failures))
    print(f'{api}: 19 actual NV2A provenance cases; matching .75/.25 depth, clear/write invalidation, '
          'shared DSVs, recreation and persistent taint checked; later green UI unchanged')


if __name__ == '__main__':
    main()
