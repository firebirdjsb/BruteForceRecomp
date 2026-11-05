"""Actual NV2A draw paths: unchanged RT0 plus an independent premultiplied HUD."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile


def run(exe, xbe, directory, mode, api, hardware):
    output = directory / (mode + '.raw')
    env = {k: v for k, v in os.environ.items() if not k.startswith(('BF_', 'RECOMP_'))}
    result = subprocess.run([str(exe), str(xbe), mode + ('-hardware' if hardware else '') + ('-d3d12' if api == 'd3d12' else ''), str(output)],
                            env=env, capture_output=True, text=True, timeout=45)
    output.with_suffix('.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, (mode, result.returncode, result.stderr[-5000:])
    data = output.read_bytes()
    assert len(data) == (640 * 480 * 4 if 'aspect' in mode else 64 * 64 * 4)
    return output, data


def main():
    exe, xbe, out = [Path(value).resolve() for value in sys.argv[1:4]]
    api = sys.argv[4] if len(sys.argv) > 4 else 'd3d11'
    hardware = '--hardware' in sys.argv[5:]
    out.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='ui-composition-', dir=out))
    for path in ('base', 'xdraw', 'vsh'):
        for kind, expected_status in (('normal', 0), ('unsupported', 1), ('clear', 3), ('reenter', 2), ('missing', None)):
            mode = f'gpu-uicompose-{path}-{kind}'
            _, original = run(exe, xbe, directory, mode + '-off', api, hardware)
            output, captured = run(exe, xbe, directory, mode, api, hardware)
            assert captured == original, f'{path}/{kind}: capture changed original RT0 bytes'
            overlay_file = Path(str(output) + '.overlay')
            if expected_status is None:
                assert not overlay_file.exists(), f'{path}/{kind}: missing boundary must use original image'
                continue
            overlay = overlay_file.read_bytes()
            generation, color_va, width, height, draws, status = struct.unpack_from('<Q5I', overlay)
            assert generation > 0 and color_va != 0 and width == height == 64
            assert status == expected_status, (path, kind, status, expected_status)
            if status:
                assert len(overlay) == 28, 'Invalid capture must explicitly reject, without claiming usable pixels'
                continue
            assert draws >= 5, (path, draws)
            assert len(overlay) == 28 + 64 * 64 * 8
            rgba = list(struct.iter_unpack('<4e', overlay[28:]))
            original_pixels = struct.unpack('<4096I', original)
            max_error = 0
            for index, (r, g, b, coverage) in enumerate(rgba):
                assert 0 <= coverage <= 1 and all(0 <= c <= 1 for c in (r, g, b))
                for channel, base, shift in ((r, 24, 16), (g, 56, 8), (b, 88, 0)):
                    composed = round(channel * 255 + base * (1 - coverage))
                    expected = (original_pixels[index] >> shift) & 255
                    max_error = max(max_error, abs(composed - expected))
            # Original RT0 rounds each 8-bit draw; the independent RGBA16 target
            # rounds half-float accumulation and quantizes once when presented.
            assert max_error <= 2, (path, 'UNORM/FP16 rounding error', max_error)
            assert rgba[8 * 64 + 8][3] == 1, f'{path}: opaque low-alpha source must fully cover the world'
            assert all(abs(value) < 1e-7 for value in rgba[32 * 64 + 48]), f'{path}: rejected stencil/depth/cutout writes leaked'
            assert .49 < rgba[32 * 64 + 16][3] < .51, f'{path}: straight-alpha coverage missing'
            # Verify the same saved overlay represents a different reconstructed
            # world: untouched/right-half pixels must expose that new world,
            # opaque pixels must remain independent of it.
            replacement = (210, 40, 70)
            empty = rgba[32 * 64 + 48]
            assert tuple(round(empty[c] * 255 + replacement[c] * (1 - empty[3])) for c in range(3)) == replacement
            opaque = rgba[8 * 64 + 8]
            assert tuple(round(opaque[c] * 255 + replacement[c] * (1 - opaque[3])) for c in range(3)) == tuple(round(opaque[c] * 255) for c in range(3))
            print(f'{api}/{path}: exact RT0, native cutout/depth/stencil/alpha/masks, affine composition <= {max_error} byte error')
        baseline, original = run(exe, xbe, directory, f'gpu-aspect-{path}-anchors', api, hardware)
        output, captured = run(exe, xbe, directory, f'gpu-aspect-{path}-anchors-capture', api, hardware)
        assert captured == original, f'{path}: anchored full HUD capture changed original RT0'
        overlay = Path(str(output) + '.overlay').read_bytes()
        generation, color_va, width, height, draws, status = struct.unpack_from('<Q5I', overlay)
        assert generation > 0 and color_va and width == 640 and height == 480 and draws == 6 and status == 0
        pixels = struct.unpack('<307200I', original)
        for index, (r, g, b, coverage) in enumerate(struct.iter_unpack('<4e', overlay[28:])):
            world = (0, 0, 255) if index % 640 < 320 else (255, 0, 0)
            composed = tuple(round(channel * 255 + world[c] * (1 - coverage)) for c, channel in enumerate((r, g, b)))
            expected = tuple((pixels[index] >> shift) & 255 for shift in (16, 8, 0))
            assert composed == expected, f'{path}: native anchor group lost position, coverage or generation'
        print(f'{api}/{path}: six original left/center/right anchored HUD groups captured in one exact 640x480 generation')
    print(f'PASS {api}: three actual draw paths, original image parity, supported HUD composition and explicit unsafe/missing-boundary fallback')


if __name__ == '__main__':
    main()
