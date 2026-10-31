"""Extract a local disc into a new private folder; never execute disc utilities."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--toolkit', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    lock = json.loads((ROOT / 'toolchain.json').read_text(encoding='utf-8'))
    toolkit, output = args.toolkit.resolve(), args.output.resolve()
    try:
        commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=toolkit, text=True).strip()
        dirty = subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=toolkit, text=True)
        if commit != lock['commit'] or dirty.strip():
            raise ValueError('Toolkit must be the clean pinned commit')
        if output.exists():
            raise ValueError('Output already exists; select a new private directory')
        sys.path.insert(0, str(toolkit))
        from tools.xiso.xdvdfs import Xiso
        with Xiso(str(args.iso.resolve())) as iso:
            entries = list(iso.walk())
            paths = []
            keys = set()
            for directory, entry in entries:
                relative = PurePosixPath(directory.replace('\\', '/')) / entry.name
                if relative.is_absolute() or any(p in ('.', '..') or ':' in p or '\\' in p for p in relative.parts):
                    raise ValueError('Unsafe path in disc directory')
                destination = output.joinpath(*relative.parts).resolve()
                if not destination.is_relative_to(output):
                    raise ValueError('Disc path leaves extraction directory')
                key = str(destination).casefold()
                if key in keys:
                    raise ValueError('Duplicate disc destination')
                keys.add(key)
                paths.append((entry, destination))
            output.parent.mkdir(parents=True, exist_ok=True)
            if sum(entry.size for entry, _ in paths) > shutil.disk_usage(output.parent).free:
                raise ValueError('Insufficient free disk space')
            executable = next((entry for entry, dest in paths if dest == output / 'default.xbe'), None)
            if executable is None:
                raise ValueError('Disc has no root default.xbe')
            output.mkdir()
            iso.extract(executable, str(output / 'default.xbe'))
            digest = hashlib.sha256((output / 'default.xbe').read_bytes()).hexdigest()
            if digest != lock['xbe_sha256']:
                raise ValueError('Disc executable does not match the supported build')
            for entry, destination in paths:
                if destination == output / 'default.xbe':
                    continue
                destination.parent.mkdir(parents=True, exist_ok=True)
                iso.extract(entry, str(destination))
            print(f'Extracted {len(paths)} files; no executable was launched.')
    except Exception as error:
        parser.exit(1, f'Extraction stopped: {error}. Partial output is retained for inspection.\n')


if __name__ == '__main__':
    main()
