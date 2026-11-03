"""The public preparation tool is tested without distributing any game files."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import prepare_dlc_pack


class PrepareDlcPackTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='bf-dlc-prepare-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.archive = self.root / 'input.zip'
        self.output = self.root / 'prepared'
        self.manifest = self.root / 'manifest.json'

    def fixture(self, entries=None, rows=None):
        payload = b'synthetic level data'
        path = '4d53001e00000001/level.tgz'
        prefix = 'fixture/content/c/'
        with zipfile.ZipFile(self.archive, 'w') as archive:
            for name, data in (entries or [(prefix + path, payload)]):
                archive.writestr(name, data)
            archive.writestr('fixture/default.xbe', b'installer must be excluded')
        profile = {
            'id': 'synthetic-dlc', 'archive_prefix': prefix,
            'archive_sha256': hashlib.sha256(self.archive.read_bytes()).hexdigest(),
            'files': rows or [{'path': path, 'size': len(payload),
                               'sha256': hashlib.sha256(payload).hexdigest()}],
        }
        self.manifest.write_text(json.dumps(profile), encoding='utf-8')

    def prepare(self):
        with patch.object(prepare_dlc_pack, 'MANIFEST', self.manifest):
            return prepare_dlc_pack.prepare(self.archive, self.output)

    def test_verified_data_only_and_original_preserved(self):
        self.fixture()
        original = self.archive.read_bytes()
        self.assertEqual(self.prepare(), 1)
        self.assertEqual((self.output / '4d53001e00000001/level.tgz').read_bytes(),
                         b'synthetic level data')
        self.assertEqual((self.output / 'bf-dlc-pack.txt').read_text(), 'synthetic-dlc\n')
        self.assertFalse(list(self.output.rglob('*.xbe')))
        self.assertEqual(self.archive.read_bytes(), original)

    def test_existing_output_preserved(self):
        self.fixture()
        self.output.mkdir()
        save = self.output / 'progress.xmb'
        save.write_bytes(b'permanent progress')
        with self.assertRaisesRegex(ValueError, 'empty output'):
            self.prepare()
        self.assertEqual(save.read_bytes(), b'permanent progress')

    def test_unknown_archive_rejected_before_writing(self):
        self.fixture()
        with self.archive.open('ab') as archive:
            archive.write(b'changed')
        with self.assertRaisesRegex(ValueError, 'archive differs'):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_corrupted_payload_rejected_before_writing(self):
        self.fixture([('fixture/content/c/4d53001e00000001/level.tgz', b'wrong')])
        with self.assertRaisesRegex(ValueError, 'file verification failed'):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_duplicate_case_insensitive_entry_rejected(self):
        self.fixture([('fixture/content/c/4d53001e00000001/level.tgz', b'a'),
                      ('fixture/content/c/4D53001E00000001/LEVEL.TGZ', b'b')])
        with self.assertRaisesRegex(ValueError, 'Duplicate case-insensitive'):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_inventory_and_path_rejected_before_writing(self):
        self.fixture([('fixture/content/c/unexpected', b'new')])
        with self.assertRaisesRegex(ValueError, 'inventory differs'):
            self.prepare()
        self.fixture(rows=[{'path': '../escape', 'size': 1, 'sha256': '0' * 64}])
        with self.assertRaisesRegex(ValueError, 'Unsafe DLC manifest path'):
            self.prepare()
        self.assertFalse(self.output.exists())
        self.assertFalse((self.root / 'escape').exists())


if __name__ == '__main__':
    unittest.main()
