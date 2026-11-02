import struct
import tempfile
import unittest
from pathlib import Path
from check_menu_frames import inspect_frame


class MenuCaptureChecks(unittest.TestCase):
    def frame(self, directory, bgr):
        header = bytearray(54)
        header[:2] = b'BM'
        struct.pack_into('<I', header, 10, 54)
        struct.pack_into('<IiiHHI', header, 14, 40, 4, 4, 1, 24, 0)
        path = Path(directory) / 'frame.bmp'
        path.write_bytes(header + bytes(bgr) * 16)
        return path

    def test_empty_chroma_green_and_blank_frames_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            for bgr in ((0, 154, 0), (0, 0, 0)):
                with self.subTest(bgr=bgr), self.assertRaises(ValueError):
                    inspect_frame(self.frame(directory, bgr))

    def test_blue_animation_sample_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            result = inspect_frame(self.frame(directory, (100, 50, 20)))
            self.assertEqual(result['green_fraction'], 0)
            self.assertEqual(result['blue_fraction'], 1)

    def test_incomplete_frame_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.frame(directory, (100, 50, 20))
            path.write_bytes(path.read_bytes()[:-1])
            with self.assertRaises(ValueError):
                inspect_frame(path)
