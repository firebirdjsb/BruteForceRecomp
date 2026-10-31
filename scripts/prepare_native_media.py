"""Preserve the original helper body and redirect its calls to native validation."""
import hashlib
import json
from pathlib import Path
import argparse
from verify_offsets import verify


def prepare(xbe, generated):
    identity = verify(xbe)
    data = Path(xbe).read_bytes()
    start, end = 0x002f801f, 0x002f81e8
    digest = 'f44421e4a03680a1170b1f18f84cb281ad2a3878831cc18e5fda23aa0c15cad2'
    section = next(s for s in identity['sections'] if int(s['guest_va'], 16) <= start
                   and end <= int(s['guest_va'], 16) + s['raw_size'])
    # Translate original guest VA into file position using its actual section.
    offset = section['file_offset'] + start - int(section['guest_va'], 16)
    if hashlib.sha256(data[offset:offset + end - start]).hexdigest() != digest:
        raise ValueError('Original media-helper body does not match authenticated evidence')
    cache_va = 0x00376c1d
    cache_section = next(s for s in identity['sections'] if int(s['guest_va'], 16) <= cache_va
                         and cache_va + 2 <= int(s['guest_va'], 16) + s['raw_size'])
    cache_offset = cache_section['file_offset'] + cache_va - int(cache_section['guest_va'], 16)
    if data[cache_offset:cache_offset + 2] != b'\x0f\x09':
        raise ValueError('Expected original WBINVD instruction was not found')
    source_name = 'void sub_002F801F(void)\n{'
    preserved_name = 'void bf_original_media_002F801F(void)\n{'
    generated = Path(generated)
    matches = []
    for file in generated.glob('recomp_*.c'):
        text = file.read_text(encoding='utf-8')
        if source_name in text or preserved_name in text:
            matches.append((file, text))
    if len(matches) != 1:
        raise ValueError('Expected exactly one generated media-helper definition')
    file, text = matches[0]
    if source_name in text:
        (generated.parent / 'upstream-media-helper.c.txt').write_text(text, encoding='utf-8')
        file.write_text(text.replace(source_name, preserved_name, 1), encoding='utf-8')
    (generated / 'native-media-profile.json').write_text(json.dumps({
        'original_entry': hex(start), 'original_end': hex(end), 'original_body_sha256': digest,
        'original_ret': '0x002F81E7: C3 (ret, no immediate)',
        'preserved_symbol': 'bf_original_media_002F801F',
        'native_symbol': 'sub_002F801F', 'user_approved': True,
        'cache_service': '0x00376C1D: 0F 09; coherent host-memory barrier',
        'scope': 'Local-file validation replaces the DVD identity service; original body retained'
    }, indent=2), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--generated', type=Path, required=True)
    args = parser.parse_args()
    try:
        prepare(args.xbe, args.generated)
    except (ValueError, OSError, StopIteration) as error:
        parser.exit(1, f'Native media preparation stopped: {error}\n')
    print('Native media profile prepared; original helper body retained.')
