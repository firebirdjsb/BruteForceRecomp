"""Exercise ordered wide markers and actual 640x480 UI framing on each draw path."""
from pathlib import Path
import os
import struct
import subprocess
import sys


def main():
    exe, xbe, out = (Path(p).resolve() for p in sys.argv[1:4])
    api = sys.argv[4] if len(sys.argv) > 4 else 'd3d11'
    out.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('BF_', 'RECOMP_'))}
    for path in ('base', 'transformed', 'vsh'):
        for anchors, enabled in ((False, False), (False, True), (True, False), (True, True)):
            mode = f'gpu-aspect-{path}' + ('-anchors' if anchors else '') + ('' if enabled else '-off')
            if api == 'd3d12':
                mode += '-d3d12'
            image = out / (mode + '.raw')
            result = subprocess.run([str(exe), str(xbe), mode, str(image)], env=env,
                                    capture_output=True, text=True, timeout=60)
            (out / (mode + '.log')).write_text(result.stdout + result.stderr)
            assert result.returncode == 0, (mode, result.returncode, result.stderr[-3000:])
            raw = image.read_bytes()
            assert len(raw) == 640 * 480 * 4
            pixels = struct.unpack('<307200I', raw)
            if anchors:
                spans = (((30, 300),), ((340, 610),), ((305, 335),), ((30, 90), (305, 335), (550, 610))) if enabled else (
                    ((40, 400),), ((240, 600),), ((300, 340),), ((40, 120), (300, 340), (520, 600)))
                for y in range(480):
                    row = tuple(0xff00ff00 if any(a <= x < b for a, b in spans[y//120]) else
                                0xff0000ff if x < 320 else 0xffff0000 for x in range(640))
                    assert pixels[y*640:(y+1)*640] == row, (mode, y)
            else:
                row = tuple(0xff00ff00 if not enabled else 0xff0000ff if x < 80 else
                            0xff00ff00 if x < 560 else 0xffff0000 for x in range(640))
                assert all(pixels[y*640:(y+1)*640] == row for y in range(480)), mode
                assert (pixels[240*640+10], pixels[240*640+320], pixels[240*640+630]) == (
                    (0xff0000ff, 0xff00ff00, 0xffff0000) if enabled else (0xff00ff00,) * 3)
            print(f'{api}/{path}/{"anchors" if anchors else "center"}/{"on" if enabled else "off"}: exact non-square world/UI pixels and source-generation metadata passed')


if __name__ == '__main__':
    main()
