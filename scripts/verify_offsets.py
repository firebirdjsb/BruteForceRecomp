"""Read-only validation of this project's original Xbox executable, not a PE."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def verify(path):
    data = Path(path).read_bytes()
    lock = json.loads((ROOT / 'toolchain.json').read_text(encoding='utf-8'))
    if hashlib.sha256(data).hexdigest() != lock['xbe_sha256']:
        raise ValueError('XBE SHA-256 does not match the supported retail build')
    if data[:4] != b'XBEH':
        raise ValueError('Not an Xbox XBE')
    # XBE header fields are little-endian 32-bit DWORDs at file offsets.
    def dword(offset):
        return struct.unpack_from('<I', data, offset)[0]
    # File +0x104: image base; +0x108: header bytes; +0x10c: image bytes.
    base, headers, image = dword(0x104), dword(0x108), dword(0x10c)
    # File +0x11c: section count; +0x120: section-table guest pointer.
    count, table = dword(0x11c), dword(0x120) - base
    # Retail entry point is XOR-encoded in the DWORD at file offset 0x128.
    entry = dword(0x128) ^ 0xa8fc57ab
    # File +0x118 points to the certificate; certificate +8 is title ID.
    certificate = dword(0x118) - base
    title = dword(certificate + 8)
    if headers > len(data) or table < 0 or table + count * 56 > headers:
        raise ValueError('Invalid XBE section header bounds')
    sections = []
    for index in range(count):
        # Each section header is 56 bytes; its first five DWORDs describe
        # flags, guest VA, virtual size, file offset and raw byte count.
        flags, va, size, raw, raw_size = struct.unpack_from('<5I', data, table + index * 56)
        if raw + raw_size > len(data) or va < base or va + size > base + image:
            raise ValueError(f'Section {index} is outside the file or guest image')
        sections.append({'index': index, 'guest_va': hex(va), 'size': size,
                         'file_offset': raw, 'raw_size': raw_size, 'flags': flags})
    if entry != int(lock['entry_point'], 16) or title != int(lock['title_id'], 16):
        raise ValueError('Unexpected title or entry point')
    if not any(s['flags'] & 4 and int(s['guest_va'], 16) <= entry <
               int(s['guest_va'], 16) + s['size'] for s in sections):
        raise ValueError('Entry point is outside executable sections')
    return {'sha256': lock['xbe_sha256'], 'title_id': hex(title),
            'entry_point': hex(entry), 'bytes': len(data), 'sections': sections,
            'validation': 'Static XBE layout only; not runtime equivalence'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', type=Path, required=True)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    try:
        result = verify(args.target)
    except (ValueError, OSError, struct.error) as error:
        parser.exit(1, f'Validation failed: {error}\n')
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"PASS: Brute Force {result['title_id']}, {len(result['sections'])} sections, entry {result['entry_point']}")


if __name__ == '__main__':
    main()
