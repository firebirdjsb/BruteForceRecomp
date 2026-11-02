"""Check selected original-menu BMP captures for green/blank video regressions.

Pass captures taken after menuBack.bik opened. This is a rendering regression
check, not a mission, input, audio, or complete-game validation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect_frame(path):
    data = Path(path).read_bytes()
    if len(data) < 54 or data[:2] != b'BM':
        raise ValueError(f'{path}: not a BMP')
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    bits = struct.unpack_from('<H', data, 28)[0]
    compression = struct.unpack_from('<I', data, 30)[0]
    if width <= 0 or not height or bits != 24 or compression:
        raise ValueError(f'{path}: expected an uncompressed 24-bit renderer capture')
    height = abs(height)
    stride = (width * 3 + 3) & ~3
    if offset + stride * height > len(data):
        raise ValueError(f'{path}: incomplete capture')
    green = blue = black = 0
    pixels = bytearray()
    for y in range(height):
        row = data[offset + y * stride:offset + y * stride + width * 3]
        pixels.extend(row)
        for x in range(0, len(row), 3):
            b, g, r = row[x:x + 3]
            # Zero Y/U/V becomes approximately RGB(0,154,0) in this Bink
            # conversion. Permit rounding without accepting this corruption.
            green += r < 8 and b < 8 and 140 <= g <= 165
            blue += b > 25 and b > r * 1.3 and b > g * 1.1
            black += max(r, g, b) < 8
    count = width * height
    result = dict(file=str(path), sha256=hashlib.sha256(pixels).hexdigest(),
                  green_fraction=green / count, blue_fraction=blue / count,
                  black_fraction=black / count)
    if result['green_fraction'] > 0.05:
        raise ValueError(f'{path}: green movie corruption ({green / count:.1%})')
    if result['black_fraction'] > 0.95 or result['blue_fraction'] < 0.05:
        raise ValueError(f'{path}: original blue menu animation is missing')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('frames', nargs='+', type=Path)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    try:
        if len(args.frames) < 2:
            raise ValueError('Select at least two menu frames to check animation changes')
        frames = [inspect_frame(path) for path in args.frames]
        if len({frame['sha256'] for frame in frames}) < 2:
            raise ValueError('The selected captures contain no animation changes')
        report = dict(status='PASS', frames=frames,
                      boundary='Selected menu captures only; gameplay and input remain separate checks')
        output = json.dumps(report, indent=2)
        if args.json:
            args.json.write_text(output + '\n', encoding='utf-8')
        print(output)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f'Menu rendering check failed: {error}\n')


if __name__ == '__main__':
    main()
