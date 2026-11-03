"""Lift measured missing entries from authenticated original machine code."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from verify_offsets import verify

# Original CRT x87 classification branches omitted by entry discovery. The
# indirect table at 0x0040E81C reached 0x002F04DC during tutorial loading.
# Boundaries include the original RET or shared tail jump, not guessed ABIs.
CRT_ENTRIES = (
    (0x002f04cb, 0x002f04d5, 'cc2d8f274cc3c1fb7e3989d9139da8a3d095efb586b7d89d23468eefa2f6b13e'),
    (0x002f04d2, 0x002f04d5, '4c76c611ad8ed759ccc6bff73c101629808203063772b87dbea3e1ace2d6a9bf'),
    (0x002f04d5, 0x002f04dc, 'c4a9e6065264eeb23acf409855fd591aa061920a1ba71326dbf62f0d8914ca25'),
    (0x002f04dc, 0x002f04e3, 'ded8c9e4660670bcd7499da0525dfaa6827ce60e227ab2b4096857f1dc9ddaca'),
    (0x002f04e3, 0x002f04ea, 'd6d08351d766f70d9b6c11cc1867e8baa26b5b0dd18b5dfe9cb3941ef9fd0491'),
)

# Combat crash: original member-function dispatcher 0x00022F70 tail-jumped to
# this eight-byte virtual thunk, merged into another discovery interval.
# Preserve its exact instructions and tail-call stack; no guessed prototype.
GAMEPLAY_ENTRIES = (
    (0x0001d8f0, 0x0001d8f8, 'ae24c26279f8dc7f0f1bff547ef015024d464df679265916ea8bf3e1265b8e64'),
)


def recover(xbe, toolkit, functions, output):
    verified = verify(xbe)
    lock = json.loads((Path(__file__).resolve().parents[1] / 'toolchain.json').read_text())
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=toolkit, text=True).strip()
    if commit != lock['commit']:
        raise ValueError('Recovery requires the pinned recompiler commit')
    start, end = 0x0037ee7c, 0x0037ef88
    digest = '5fa2203370ec405e1dc3ebd5668a8f80fe3b30332ba5d7fde4c68ec8abe54c2c'
    data = Path(xbe).read_bytes()
    section = next(s for s in verified['sections'] if int(s['guest_va'], 16) <= start
                   and end <= int(s['guest_va'], 16) + s['raw_size'])
    offset = section['file_offset'] + start - int(section['guest_va'], 16)
    if hashlib.sha256(data[offset:offset + end - start]).hexdigest() != digest:
        raise ValueError('Original missing callback bytes do not match the Ghidra evidence')
    for address, opcode in ((0x00384fa5, 0xfa), (0x00384fb6, 0xfb)):
        owner = next(s for s in verified['sections'] if int(s['guest_va'], 16) <= address
                     < int(s['guest_va'], 16) + s['raw_size'])
        position = owner['file_offset'] + address - int(owner['guest_va'], 16)
        if data[position] != opcode:
            raise ValueError('Original USB interrupt-mask instruction changed')
    database = {}
    for row in json.loads(Path(functions).read_text(encoding='utf-8')):
        address = int(row['start'], 16)
        row['_addr'] = address
        row['end'] = int(row['end'], 16)
        database[address] = row
    database[start] = {'start': hex(start), 'end': end, 'size': end - start,
                       'name': 'sub_0037EE7C', '_addr': start, 'section': 'XPP'}
    for first, last, body_hash in CRT_ENTRIES + GAMEPLAY_ENTRIES:
        owner = next(s for s in verified['sections'] if int(s['guest_va'],16) <= first
                     and last <= int(s['guest_va'],16)+s['raw_size'])
        position = owner['file_offset'] + first - int(owner['guest_va'],16)
        if hashlib.sha256(data[position:position+last-first]).hexdigest() != body_hash:
            raise ValueError(f'Recovered entry {first:08X} differs from authenticated instructions')
        database[first] = {'start': hex(first), 'end': last, 'size': last-first,
                           'name': f'sub_{first:08X}', '_addr': first, 'section': '.text'}
    sys.path.insert(0, str(Path(toolkit).resolve()))
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    config.configure_from_xbe(str(xbe))
    translator = FunctionTranslator(data, database)
    code = translator.translate_function(start, database[start])
    if not code:
        raise ValueError('Callback could not be translated; no replacement was emitted')
    for first, _, _ in CRT_ENTRIES:
        branch = translator.translate_function(first,database[first])
        if not branch or any(marker in branch for marker in
                             ('Unsupported instruction','FAILED:','RECOMP_UNIMPL(','recomp_unimpl(')):
            raise ValueError(f'CRT branch {first:08X} could not be translated completely')
        code += '\n' + branch
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'recomp_recovered.c').write_text(
        '#define RECOMP_GENERATED_CODE\n#include "recomp_funcs.h"\n' + code + '\n', encoding='utf-8')
    gameplay = []
    for first, _, _ in GAMEPLAY_ENTRIES:
        entry = translator.translate_function(first, database[first])
        if not entry or any(marker in entry for marker in
                            ('Unsupported instruction', 'FAILED:', 'RECOMP_UNIMPL(', 'recomp_unimpl(')):
            raise ValueError(f'Gameplay entry {first:08X} could not be translated completely')
        gameplay.append(entry)
    (output / 'recomp_gameplay.c').write_text(
        '#define RECOMP_GENERATED_CODE\n#include "recomp_funcs.h"\n' + '\n'.join(gameplay) + '\n', encoding='utf-8')
    (output / 'provenance.json').write_text(json.dumps({
        'entry': hex(start), 'end': hex(end), 'body_sha256': digest,
        'reason': 'Measured unresolved indirect call during native startup',
        'evidence': 'Original XBE bytes match the 268-byte Ghidra MU_Init body',
        'runtime_equivalence': 'UNVERIFIED',
        'crt_branches': [dict(entry=hex(first),end=hex(last),body_sha256=body_hash)
                         for first,last,body_hash in CRT_ENTRIES],
        'gameplay_entries': [dict(entry=hex(first), end=hex(last), body_sha256=body_hash,
                                 reason='Measured missing virtual tail-dispatch thunk during combat')
                             for first,last,body_hash in GAMEPLAY_ENTRIES]
    }, indent=2), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'toolkit', 'functions', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    recover(args.xbe.resolve(), args.toolkit.resolve(), args.functions.resolve(), args.output.resolve())
    print('Authenticated startup, CRT and gameplay entries translated; output remains private.')
