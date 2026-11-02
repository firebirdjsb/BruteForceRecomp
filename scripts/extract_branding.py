"""Extract the supported XBE's authentic title icon into private build assets."""
import argparse
from pathlib import Path
import struct
import zlib
from verify_offsets import verify


def decode_title_icon(xbe):
    verify(xbe)
    data = Path(xbe).read_bytes()
    u32 = lambda offset: struct.unpack_from('<I', data, offset)[0]
    base, count, table = u32(0x104), u32(0x11c), u32(0x120) - u32(0x104)
    packed = None
    for index in range(count):
        header = table + index * 56
        name = u32(header + 20) - base
        if not 0 <= name < len(data):
            raise ValueError('Invalid section name pointer')
        if data[name:name + 32].split(b'\0', 1)[0] == b'$$XTIMAGE':
            raw, length = u32(header + 12), u32(header + 16)
            packed = data[raw:raw + length]
    if not packed or packed[:4] != b'XPR0':
        raise ValueError('Supported title image resource not found')
    total, start = struct.unpack_from('<II', packed, 4)
    texture_format = struct.unpack_from('<I', packed, 24)[0]
    width, height = 1 << ((texture_format >> 20) & 15), 1 << ((texture_format >> 24) & 15)
    if ((texture_format >> 8) & 255) != 0x0c or width > 1024 or height > 1024 or total > len(packed):
        raise ValueError('Unsupported title texture layout')
    # Xbox DXT1/BC1 blocks are 4x4 pixels, 8 bytes each: two little-endian
    # RGB565 endpoints and sixteen two-bit selectors. DXT1 uses block-linear
    # storage here, not the uncompressed Xbox Morton-order texture layout.
    size = ((width + 3) // 4) * ((height + 3) // 4) * 8
    if start < 32 or start + size > total:
        raise ValueError('Title texture data is truncated')
    pixels = bytearray(width * height * 4)
    offset = start
    def rgb(value):
        return ((value >> 11) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31, 255)
    for block_y in range(0, height, 4):
        for block_x in range(0, width, 4):
            c0, c1, selectors = struct.unpack_from('<HHI', packed, offset)
            offset += 8
            a, b = rgb(c0), rgb(c1)
            if c0 > c1:
                colors = (a, b, tuple((2 * a[i] + b[i]) // 3 for i in range(3)) + (255,),
                          tuple((a[i] + 2 * b[i]) // 3 for i in range(3)) + (255,))
            else:
                colors = (a, b, tuple((a[i] + b[i]) // 2 for i in range(3)) + (255,), (0, 0, 0, 0))
            for n in range(16):
                x, y = block_x + n % 4, block_y + n // 4
                if x < width and y < height:
                    position = (y * width + x) * 4
                    pixels[position:position + 4] = bytes(colors[(selectors >> (n * 2)) & 3])
    return width, height, pixels


def extract(xbe, output):
    width, height, pixels = decode_title_icon(xbe)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    rows = b''.join(b'\0' + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
    (output / 'title.png').write_bytes(png)
    icon = struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', width % 256, height % 256, 0, 0, 1, 32, len(png), 22) + png
    (output / 'bruteforce.ico').write_bytes(icon)
    # A native SDL bitmap, with no additional image-decoder dependency.
    bgra = b''.join(bytes((pixels[i + 2], pixels[i + 1], pixels[i], pixels[i + 3]))
                    for y in range(height - 1, -1, -1) for i in range(y * width * 4, (y + 1) * width * 4, 4))
    bmp = b'BM' + struct.pack('<IHHI', 54 + len(bgra), 0, 0, 54) + struct.pack('<IiiHHIIiiII', 40, width, height, 1, 32, 0, len(bgra), 2835, 2835, 0, 0) + bgra
    (output / 'title.bmp').write_bytes(bmp)
    return width, height


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print('Private title icon extracted:', extract(args.xbe, args.output))
