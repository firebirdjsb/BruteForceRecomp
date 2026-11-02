"""Launcher trust boundaries: requests, library containment and launch gates."""
import http.client
import json
from pathlib import Path
import sys
import types
import tempfile
import threading
import unittest
from unittest.mock import patch
from launcher.library import Library
from launcher.server import make_server
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


class LocalLauncher(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.library = Library(self.root, self.root / 'toolkit', None)
        self.server = make_server(self.library)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.directory.cleanup()

    def request(self, path, method='GET', body=None, **headers):
        connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=3)
        connection.request(method, path, body=body, headers=headers)
        response = connection.getresponse()
        result = response.status, response.read()
        connection.close()
        return result

    def test_local_state_requires_session_token(self):
        self.assertEqual(self.request('/api/state')[0], 403)
        status, body = self.request('/api/state', **{'X-Launcher-Token': self.server.token})
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)['phase'], 'empty')

    def test_cross_origin_and_rebound_host_are_rejected(self):
        for headers in ({'Origin': 'https://example.invalid'}, {'Host': 'example.invalid'}):
            status, _ = self.request('/api/launch', 'POST', '{}', **{
                'X-Launcher-Token': self.server.token, 'Content-Type': 'application/json', **headers})
            self.assertEqual(status, 403)
        self.assertIsNone(self.library.process)

    def test_private_files_are_not_http_assets(self):
        secret = self.root / 'private.txt'
        secret.write_text('private-library-data')
        for path in ('/../private.txt', '/api/../private.txt', '/game-data/default.xbe'):
            status, body = self.request(path, **{'X-Launcher-Token': self.server.token})
            self.assertEqual(status, 404)
            self.assertNotIn(b'private-library-data', body)

    def test_import_and_native_validation_are_required_before_launch(self):
        with patch('launcher.library.subprocess.Popen') as process:
            with self.assertRaisesRegex(ValueError, 'Import'):
                self.library.launch()
            self.library.game = self.root / 'imports' / 'not-verified'
            with self.assertRaisesRegex(ValueError, 'Verify'):
                self.library.launch()
            process.assert_not_called()

    def test_incomplete_import_is_not_published(self):
        iso = self.root / 'fixture.iso'
        iso.write_bytes(b'not-a-disc')
        done = threading.Event()
        def fail(*args):
            done.set()
            raise ValueError('Unsupported test disc')
        with patch('launcher.library.extract_disc', side_effect=fail):
            self.library.import_iso(iso)
            self.assertTrue(done.wait(2))
            # Taking the lock waits for any final state update without a sleep.
            with self.library.lock:
                pass
        self.assertFalse((self.root / 'library.json').exists())
        self.assertIsNone(self.library.game)

    def test_saved_library_cannot_escape_import_directory(self):
        elsewhere = self.root / 'other-installation'
        elsewhere.mkdir()
        (self.root / 'library.json').write_text(json.dumps({'game_dir': str(elsewhere)}))
        library = Library(self.root, self.root / 'toolkit', None)
        self.assertIsNone(library.game)
        self.assertEqual(library.snapshot()['phase'], 'error')


if __name__ == '__main__':
    unittest.main()
