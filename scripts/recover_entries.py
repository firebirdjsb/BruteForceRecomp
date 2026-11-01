"""Lift a measured missing callback from authenticated original machine code."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from verify_offsets import verify


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
    sys.path.insert(0, str(Path(toolkit).resolve()))
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    config.configure_from_xbe(str(xbe))
    translator = FunctionTranslator(data, database)
    code = translator.translate_function(start, database[start])
    if not code:
        raise ValueError('Callback could not be translated; no replacement was emitted')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'recomp_recovered.c').write_text(
        '#define RECOMP_GENERATED_CODE\n#include "recomp_funcs.h"\n' + code + '\n', encoding='utf-8')
    (output / 'provenance.json').write_text(json.dumps({
        'entry': hex(start), 'end': hex(end), 'body_sha256': digest,
        'reason': 'Measured unresolved indirect call during native startup',
        'evidence': 'Original XBE bytes match the 268-byte Ghidra MU_Init body',
        'runtime_equivalence': 'UNVERIFIED'
    }, indent=2), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'toolkit', 'functions', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    recover(args.xbe.resolve(), args.toolkit.resolve(), args.functions.resolve(), args.output.resolve())
    print('Authenticated missing startup callback translated; output remains private.')
