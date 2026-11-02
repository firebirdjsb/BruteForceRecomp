"""Extract a local disc into a new private folder; never execute disc utilities."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RESERVED = {'CON', 'PRN', 'AUX', 'NUL'} | {f'{name}{n}' for name in ('COM', 'LPT') for n in range(1, 10)}


def is_demo_payload(relative):
    """Exclude only independently identified ancillary demo payloads.

    See Tzeentchnet/BruteForce docs/ARCHITECTURE.md. Do not infer that
    similarly named main-game archives or game resources are demos.
    """
    parts = relative.parts
    if not parts:
        return False
    first = parts[0].casefold()
    return first in ('fx', 'mech') or (len(parts) == 1 and first == 'demos.xbe')


def disc_path(directory, filename):
    """Use one portable path policy so Windows/Linux imports agree."""
    if not filename or '/' in filename or '\\' in filename:
        raise ValueError('Unsafe filename in disc directory')
    raw = (directory.replace('\\', '/') + '/' + filename).lstrip('/') if not directory.startswith(('/', '\\')) else ''
    if not raw:
        raise ValueError('Absolute path in disc directory')
    parts = raw.split('/')
    if any(not part or part in ('.', '..') or part[-1:] in (' ', '.') or
           any(ord(char) < 32 or char in '<>:"|?*' for char in part) or
           part.split('.', 1)[0].upper() in RESERVED for part in parts):
        raise ValueError('Unsafe path in disc directory')
    return PurePosixPath(*parts)


def extract_disc(iso_path, toolkit, output, progress=None):
    """Extract to a new directory, reporting (copied bytes, total, filename)."""
    iso_path = Path(iso_path).resolve(strict=True)
    if not iso_path.is_file():
        raise ValueError('Select a local ISO file')
    lock = json.loads((ROOT / 'toolchain.json').read_text(encoding='utf-8'))
    toolkit, output = Path(toolkit).resolve(), Path(output).resolve()
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=toolkit, text=True).strip()
    dirty = subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=toolkit, text=True)
    if commit != lock['commit'] or dirty.strip():
        raise ValueError('Toolkit must be the clean pinned commit')
    if output.exists():
        raise ValueError('Output already exists; select a new private directory')
    sys.path.insert(0, str(toolkit))
    from tools.xiso.xdvdfs import Xiso
    with Xiso(str(iso_path)) as iso:
        entries = list(iso.walk())
        paths = []
        skipped_demo_files = 0
        keys = set()
        for directory, entry in entries:
            relative = disc_path(directory, entry.name)
            destination = output.joinpath(*relative.parts).resolve()
            if not destination.is_relative_to(output):
                raise ValueError('Disc path leaves extraction directory')
            key = str(destination).casefold()
            if key in keys:
                raise ValueError('Duplicate disc destination')
            keys.add(key)
            if is_demo_payload(relative):
                skipped_demo_files += 1
                continue
            paths.append((entry, destination))
        output.parent.mkdir(parents=True, exist_ok=True)
        total = sum(entry.size for entry, _ in paths)
        if total > shutil.disk_usage(output.parent).free:
            raise ValueError('Insufficient free disk space')
        executable = next((entry for entry, dest in paths if dest == output / 'default.xbe'), None)
        if executable is None:
            raise ValueError('Disc has no root default.xbe')
        output.mkdir()
        iso.extract(executable, str(output / 'default.xbe'))
        digest = hashlib.sha256((output / 'default.xbe').read_bytes()).hexdigest()
        if digest != lock['xbe_sha256']:
            raise ValueError('Disc executable does not match the supported build')
        copied = executable.size
        if progress:
            progress(copied, total, 'default.xbe')
        for entry, destination in paths:
            if destination == output / 'default.xbe':
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            iso.extract(entry, str(destination))
            copied += entry.size
            if progress:
                progress(copied, total, destination.relative_to(output).as_posix())
        return {'files': len(paths), 'demo_files_excluded': skipped_demo_files,
                'bytes': copied, 'xbe_sha256': digest}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--toolkit', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = extract_disc(args.iso, args.toolkit, args.output)
        print(f"Extracted {result['files']} game files; excluded "
              f"{result['demo_files_excluded']} ancillary demo files; no executable was launched.")
    except Exception as error:
        parser.exit(1, f'Extraction stopped: {error}. Partial output is retained for inspection.\n')


if __name__ == '__main__':
    main()
