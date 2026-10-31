"""Inventory translation gaps and turn unresolved-target returns into hard failures."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def audit(directory):
    directory = Path(directory)
    files = sorted(directory.glob('*.c'))
    if not files:
        raise ValueError('No generated C files to audit')
    unresolved = directory / 'recomp_stubs_unresolved.c'
    targets = []
    if unresolved.exists():
        text = unresolved.read_text(encoding='utf-8')
        marker = re.compile(r'RECOMP_STUB_HIT\((0x[0-9A-Fa-f]+u)\);')
        targets = marker.findall(text)
        if targets:
            original = directory.parent / 'upstream-unresolved.c.txt'
            original.write_text(text, encoding='utf-8')
            hardened = marker.sub(r'recomp_unimpl("Unresolved direct-call target", \1);', text)
            # Original addresses/bodies are retained privately for comparison.
            # The host's recomp_unimpl terminates; no unknown body returns success.
            unresolved.write_text(hardened, encoding='utf-8')
        else:
            targets = re.findall(r'recomp_unimpl\("Unresolved direct-call target", (0x[0-9A-Fa-f]+u)\);', text)
    failures = []
    for file in files:
        source = file.read_text(encoding='utf-8')
        pattern = re.compile(r'/\* FAILED: (\w+) at (0x[0-9A-Fa-f]+) \*/\nvoid \1\(void\) \{ /\* translation failed \*/ \}')
        matches = pattern.findall(source)
        if matches:
            backup = directory.parent / 'upstream-failed'
            backup.mkdir(exist_ok=True)
            (backup / (file.name + '.txt')).write_text(source, encoding='utf-8')
            source = pattern.sub(r'/* FAILED: \1 at \2 */\nvoid \1(void) { recomp_unimpl("Translation failed", \2u); }', source)
            file.write_text(source, encoding='utf-8')
        failures.extend(re.findall(r'recomp_unimpl\("Translation failed", (0x[0-9A-Fa-f]+u)\);', source))
    report = {'unresolved_direct_targets': targets, 'unresolved_direct_count': len(targets),
              'failed_translation_targets': failures, 'failed_translation_count': len(failures),
              'files': [], 'status': 'NOT_RUNTIME_VERIFIED'}
    for file in files:
        data = file.read_bytes()
        text = data.decode('utf-8')
        report['files'].append({'name': file.name, 'bytes': len(data),
                                'sha256': hashlib.sha256(data).hexdigest(),
                                'unsupported_markers': len(re.findall(r'RECOMP_UNIMPL\(', text))})
    report['unsupported_markers'] = sum(f['unsupported_markers'] for f in report['files'])
    (directory.parent / 'generation-audit.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        result = audit(args.directory)
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + '\n')
    print(f"Unresolved direct targets: {result['unresolved_direct_count']}; unsupported markers: {result['unsupported_markers']}")
