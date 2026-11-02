import io
import json
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from importlib.util import module_from_spec, spec_from_file_location

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "frame_bench.py"
SPEC = spec_from_file_location("frame_bench", MODULE_PATH)
mod = module_from_spec(SPEC)
SPEC.loader.exec_module(mod)
HEADER = "flip,t_us,draws,program_vertices,hash,distinct,constants\n"

class FrameBenchTests(unittest.TestCase):
    def test_reports_true_distinct_interval_lower_bound(self):
        with tempfile.TemporaryDirectory() as tmp:
            file = Path(tmp) / "frames.csv"
            file.write_text(HEADER + "\n".join((
                "1,1000000,1,2,000000000000000a,1,1",
                "2,1500000,3,4,000000000000000a,0,1",
                "3,2000000,4,5,000000000000000b,1,2",
                "4,2500000,2,1,0000000000000000,-1,3",
            )) + "\n", encoding="utf-8")
            report = mod.summarize(file)
            self.assertEqual(report["flip_intervals"], 3)
            self.assertEqual(report["flip_hz"], 2)
            self.assertEqual(report["distinct_image_intervals"], 1)
            self.assertEqual(report["unknown_image_intervals"], 1)
            self.assertEqual(report["draws"], 9)
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertEqual(mod.main([str(file)]), 0)
            self.assertFalse(json.loads(output.getvalue())["comparison_validated"])

    def test_rejects_invalid_or_unsafely_interpreted_frames(self):
        samples = (
            HEADER + "1,1,0,0,0,1,0\n2,2,0,0,0,-1,0\n",
            HEADER + "1,1,0,0,0,-1,0\n1,2,0,0,0,-1,0\n",
            HEADER + "1,1,0,0,0,-1,0\n2,1,0,0,0,-1,0\n",
            HEADER + "1,1,0,0,0,-1,0\n",
        )
        with tempfile.TemporaryDirectory() as tmp:
            file = Path(tmp) / "bad.csv"
            for contents in samples:
                file.write_text(contents, encoding="utf-8")
                with self.assertRaises(ValueError):
                    mod.summarize(file)

if __name__ == "__main__":
    unittest.main()
