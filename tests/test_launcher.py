"""Portable disc extraction path validation."""
import unittest
from extract_disc import disc_path


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


if __name__ == '__main__':
    unittest.main()
