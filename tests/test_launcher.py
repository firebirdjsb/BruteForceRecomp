"""Portable disc paths and demo-only extraction exclusions."""
import json
from pathlib import Path
import sys
import types
import tempfile
import unittest
from unittest.mock import patch
from extract_disc import disc_path, is_demo_payload, extract_disc


class DiscPaths(unittest.TestCase):
    def test_portable_disc_paths(self):
        self.assertEqual(disc_path('data\\common', 'level.tgz').as_posix(), 'data/common/level.tgz')
        self.assertEqual(disc_path('', 'default.xbe').as_posix(), 'default.xbe')

    def test_device_names_traversal_and_platform_aliases_are_rejected(self):
        for directory, name in (('../outside', 'file'), ('/absolute', 'file'), ('data', '../file'),
                                ('data', 'CON.txt'), ('data', 'lpt1'), ('data', 'file:stream'),
                                ('data', 'trailing.')):
            with self.subTest(directory=directory, name=name), self.assertRaises(ValueError):
                disc_path(directory, name)


class AncillaryDemoExclusion(unittest.TestCase):
    def test_identified_demo_directories_and_loader_are_excluded(self):
        for directory, filename in (
            ('fx', 'fx.xbe'), ('fx/data', 'fx.jwad'),
            ('Mech', 'Mech.xbe'), ('Mech/subfolder', 'resource.mgf'),
            ('', 'demos.xbe'), ('', 'DEMOS.XBE'),
        ):
            with self.subTest(directory=directory, filename=filename):
                self.assertTrue(is_demo_payload(disc_path(directory, filename)))

    def test_original_game_resources_and_similarly_named_paths_remain(self):
        for directory, filename in (
            ('', 'default.xbe'), ('data', 'common.tgz'),
            ('data', 'tutorial.tgz'), ('data/ml-sounds/en', 'voices.tgz'),
            ('data/movies', 'intro.bik'), ('music', 'song.xwb'),
            ('fxextra', 'game.tgz'), ('Mech-assets', 'models.bin'),
            ('', 'update.xbe'), ('', 'dashupdate.xbe'),
        ):
            with self.subTest(directory=directory, filename=filename):
                self.assertFalse(is_demo_payload(disc_path(directory, filename)))


class FilteredDiscExtraction(unittest.TestCase):
    def test_new_import_never_extracts_identified_demo_files(self):
        """Synthetic XISO: exercise extraction, not only the path predicate."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / 'fixture.iso'
            image.write_bytes(b'synthetic-iso')
            output = root / 'game'
            toolkit = root / 'toolkit'
            toolkit.mkdir()

            class Entry:
                def __init__(self, name):
                    self.name = name
                    self.size = len(name.encode('utf-8')) + 1

            entries = [('', 'default.xbe'), ('data', 'common.tgz'),
                       ('data', 'tutorial.tgz'), ('fx', 'fx.xbe'),
                       ('Mech', 'Mech.xbe'), ('', 'demos.xbe')]

            class FakeXiso:
                def __init__(self, path):
                    self.path = path
                def __enter__(self):
                    return self
                def __exit__(self, *args):
                    return False
                def walk(self):
                    for location, name in entries:
                        yield location, Entry(name)
                def extract(self, entry, destination):
                    Path(destination).write_bytes(entry.name.encode('utf-8') + b'!')

            xiso = types.ModuleType('tools.xiso.xdvdfs')
            xiso.Xiso = FakeXiso
            package = types.ModuleType('tools')
            package.__path__ = []
            subpackage = types.ModuleType('tools.xiso')
            subpackage.__path__ = []

            saved_path = sys.path[:]
            try:
                with patch.dict(sys.modules, {'tools': package, 'tools.xiso': subpackage,
                                              'tools.xiso.xdvdfs': xiso}), \
                     patch('extract_disc.subprocess.check_output',
                           side_effect=['pinned-commit', '']), \
                     patch('extract_disc.hashlib.sha256') as digest, \
                     patch('extract_disc.ROOT', root):
                    (root / 'toolchain.json').write_text(json.dumps({
                        'commit': 'pinned-commit', 'xbe_sha256': 'synthetic-digest'}))
                    digest.return_value.hexdigest.return_value = 'synthetic-digest'
                    result = extract_disc(image, toolkit, output)
            finally:
                sys.path[:] = saved_path

            self.assertEqual(result['files'], 3)
            self.assertEqual(result['demo_files_excluded'], 3)
            self.assertTrue((output / 'default.xbe').is_file())
            self.assertTrue((output / 'data' / 'common.tgz').is_file())
            self.assertTrue((output / 'data' / 'tutorial.tgz').is_file())
            self.assertFalse((output / 'fx').exists())
            self.assertFalse((output / 'Mech').exists())
            self.assertFalse((output / 'demos.xbe').exists())


if __name__ == '__main__':
    unittest.main()
