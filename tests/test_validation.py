"""Offline checks; native tests use explicitly supplied private inputs."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('audit_generated', ROOT / 'scripts/audit_generated.py')
audit_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit_module)


class TranslationSafety(unittest.TestCase):
    def test_unknown_targets_stop_and_original_output_is_retained(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / 'generated'
            directory.mkdir()
            original = 'void sub_00123456(void) { RECOMP_STUB_HIT(0x00123456u); g_esp += 4; }\n'
            unknown = directory / 'recomp_stubs_unresolved.c'
            unknown.write_text(original, encoding='utf-8')
            failed = directory / 'recomp_0000.c'
            failed.write_text('/* FAILED: sub_00123460 at 0x00123460 */\nvoid sub_00123460(void) { /* translation failed */ }\n', encoding='utf-8')
            report = audit_module.audit(directory)
            self.assertEqual(report['unresolved_direct_count'], 1)
            self.assertEqual(report['failed_translation_count'], 1)
            self.assertIn('recomp_unimpl("Unresolved direct-call target", 0x00123456u);', unknown.read_text())
            self.assertIn('recomp_unimpl("Translation failed", 0x00123460u);', failed.read_text())
            self.assertEqual((directory.parent / 'upstream-unresolved.c.txt').read_text(), original)
            before = unknown.read_bytes(), failed.read_bytes()
            audit_module.audit(directory)
            self.assertEqual(before, (unknown.read_bytes(), failed.read_bytes()))


class NativeMediaValidation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        executable, data = os.environ.get('BF_TEST_EXE'), os.environ.get('BF_GAME_DIR')
        if not executable or not data:
            raise unittest.SkipTest('Set BF_TEST_EXE and BF_GAME_DIR for native input checks')
        cls.executable, cls.data = Path(executable).resolve(), Path(data).resolve()

    def validate(self, directory):
        return subprocess.run([str(self.executable), '--validate-only', str(directory)],
                              capture_output=True, text=True, timeout=20)

    def test_supported_inputs(self):
        result = self.validate(self.data)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_asset_is_rejected_without_guest_execution(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            shutil.copyfile(self.data / 'default.xbe', directory / 'default.xbe')
            self.assertEqual(self.validate(directory).returncode, 11)

    def test_single_byte_asset_corruption_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name in ('default.xbe', 'data/common.tgz', 'data/tutorial.tgz'):
                destination = directory / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(self.data / name, destination)
            with (directory / 'data/common.tgz').open('r+b') as file:
                first = file.read(1)
                file.seek(0)
                file.write(bytes([first[0] ^ 1]))
            result = self.validate(directory)
            self.assertEqual(result.returncode, 11)
            self.assertIn('Unsupported or damaged file: data/common.tgz', result.stderr)


if __name__ == '__main__':
    unittest.main()
