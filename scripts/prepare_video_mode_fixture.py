"""Prepare a private original-code mode-selection fixture from exact local inputs."""
import argparse
import hashlib
from pathlib import Path
from verify_offsets import verify


def prepare(xbe, generated, output):
    profile = verify(xbe)
    data = xbe.read_bytes()
    specs = {
        'recomp_0219.c': ('0cbf09095d000a59ef1d22c8d2d7dff81cc476da49e060ccfb7b33d0eef15fa8', ['00373AC0']),
        'recomp_0220.c': ('e2c8222e2dfe2f88d8ffe34e0fb2e97b813425d70228b3970ab43acba2568fc9', ['003783A1', '003783C0', '00378439']),
    }
    result = '#define RECOMP_GENERATED_CODE\n#include "recomp_funcs.h"\n'
    for name, (digest, entries) in specs.items():
        text = (generated / name).read_text(encoding='utf-8')
        if hashlib.sha256(text.encode()).hexdigest() != digest:
            raise ValueError(f'Unaudited original mode code: {name}')
        for entry in entries:
            start = text.index(f'void sub_{entry}(void)\n{{')
            end = text.index('\n/**', start)
            result += text[start:end] + '\n'
    # Original mode rows plus pixel-format switch tables. These bytes stay in
    # the ignored build tree and are never included in public source/packages.
    result += 'void bf_video_fixture_data(unsigned char *memory) {\n'
    for va, size in [(0x37A420, 0x8AC), (0x373B0C, 0x54)]:
        section = next(s for s in profile['sections'] if int(s['guest_va'], 16) <= va and
                       va + size <= int(s['guest_va'], 16) + s['raw_size'])
        at = section['file_offset'] + va - int(section['guest_va'], 16)
        values = ','.join(f'0x{b:02x}' for b in data[at:at + size])
        result += f'{{ static const unsigned char bytes[]={{{values}}}; memcpy(memory+0x{va:X},bytes,sizeof bytes); }}\n'
    result += '}\n'
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text(encoding='utf-8') != result:
        output.write_text(result, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--generated', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.xbe, args.generated, args.output)
