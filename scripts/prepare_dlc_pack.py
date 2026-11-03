"""Prepare the supplied, authenticated DLC data for a local native launcher build."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

MANIFEST = Path(__file__).resolve().parents[1] / 'content/dlc-manifest.json'


def prepare(archive, output):
    profile = json.loads(MANIFEST.read_text(encoding='utf-8'))
    archive, output = Path(archive), Path(output)
    for row in profile['files']:
        relative = Path(row['path'])
        if (relative.is_absolute() or '..' in relative.parts or
                ':' in row['path'] or '\\' in row['path']):
            raise ValueError('Unsafe DLC manifest path')
    if hashlib.sha256(archive.read_bytes()).hexdigest() != profile['archive_sha256']:
        raise ValueError('DLC archive differs from the audited supplied package')
    if output.exists() and any(output.iterdir()):
        raise ValueError('Choose an empty output directory; existing content is preserved')
    with zipfile.ZipFile(archive) as source:
        entries = {}
        for info in source.infolist():
            if not info.filename.startswith(profile['archive_prefix']) or info.is_dir():
                continue
            relative = info.filename[len(profile['archive_prefix']):].lower()
            if relative in entries:
                raise ValueError('Duplicate case-insensitive DLC archive entry')
            entries[relative] = info
        expected = {row['path'] for row in profile['files']}
        if set(entries) != expected:
            raise ValueError('DLC data inventory differs from the audited package')
        # Authenticate the complete input before writing any prepared content.
        for row in profile['files']:
            data = source.read(entries[row['path']])
            if len(data) != row['size'] or hashlib.sha256(data).hexdigest() != row['sha256']:
                raise ValueError('DLC file verification failed: ' + row['path'])
        output.mkdir(parents=True, exist_ok=True)
        for row in profile['files']:
            relative = Path(row['path'])
            destination = output / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(source.read(entries[row['path']]))
        (output / 'bf-dlc-pack.txt').write_text(profile['id'] + '\n', encoding='utf-8')
    return len(profile['files'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zip', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        count = prepare(args.zip, args.output)
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        parser.exit(1, f'DLC preparation stopped: {error}\n')
    print(f'Prepared and verified {count} DLC data files. Xbox installers/updates were excluded.')
