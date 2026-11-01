"""Checked disc import and native process lifecycle; no game files are served."""
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from extract_disc import extract_disc


class Library:
    def __init__(self, workspace, toolkit, executable):
        self.workspace = Path(workspace).resolve()
        self.toolkit = Path(toolkit).resolve()
        self.executable = Path(executable).resolve() if executable else None
        self.workspace.mkdir(parents=True, exist_ok=True)
        self.lock = threading.Lock()
        self.process = None
        self.busy = False
        self.game = None
        self.data = dict(phase='empty', message='Your squad is waiting. Import your Brute Force disc to begin.',
                         progress=0, current_file='', game_dir='', error='', log='', running=False,
                         executable=str(self.executable or ''), native_available=bool(self.executable and self.executable.is_file()),
                         imported_files=0)
        config = self.workspace / 'library.json'
        if config.exists():
            try:
                saved = json.loads(config.read_text(encoding='utf-8'))
                game = Path(saved['game_dir']).resolve(strict=True)
                # Restore only our own completed imports. Never trust an
                # arbitrary external directory supplied by a stale config.
                if not game.is_relative_to(self.workspace / 'imports'):
                    raise ValueError('Saved import leaves the launcher library')
                self.game = game
                self.data.update(phase='imported', game_dir=str(game), message='Disc imported. Verify the native build before deployment.',
                                 imported_files=saved.get('files', 0))
            except (OSError, ValueError, KeyError) as error:
                self.data.update(phase='error', error=str(error), message='The saved import needs attention.')

    def snapshot(self):
        with self.lock:
            return dict(self.data)

    def update(self, **values):
        with self.lock:
            self.data.update(values)

    def begin(self, phase, message):
        with self.lock:
            if self.busy or self.data['running']:
                raise ValueError('Wait for the current operation or game to finish')
            self.busy = True
            self.data.update(phase=phase, message=message, error='', progress=0, current_file='')

    def native_validate(self, game):
        if not self.executable or not self.executable.is_file():
            raise ValueError('Native executable not found. Start the launcher with --exe pointing to your built host.')
        result = subprocess.run([str(self.executable), '--validate-only', str(game)],
                                capture_output=True, text=True, errors='replace', timeout=30)
        if result.returncode:
            raise ValueError((result.stderr or result.stdout or 'Native input validation failed')[-2000:])

    def import_iso(self, iso):
        iso = Path(iso).expanduser().resolve(strict=True)
        if not iso.is_file() or iso.suffix.lower() not in ('.iso', '.xiso'):
            raise ValueError('Select a local .iso or .xiso disc image')
        self.begin('importing', 'Reading your disc. Keep the launcher open until the import finishes.')

        def run():
            output = self.workspace / 'imports' / ('disc-' + uuid.uuid4().hex[:12])
            try:
                def progress(copied, total, name):
                    self.update(progress=round(copied / max(total, 1) * 100, 1), current_file=name)
                result = extract_disc(iso, self.toolkit, output, progress)
                self.update(phase='verifying', message='Checking the executable and required archives.', progress=100)
                # An import can be retained without a build, but Start stays
                # disabled until the native validator accepts the local files.
                ready = bool(self.executable and self.executable.is_file())
                if ready:
                    self.native_validate(output)
                record = dict(game_dir=str(output), files=result['files'], xbe_sha256=result['xbe_sha256'])
                temporary = self.workspace / 'library.json.new'
                temporary.write_text(json.dumps(record, indent=2), encoding='utf-8')
                temporary.replace(self.workspace / 'library.json')
                self.game = output
                self.update(phase='ready' if ready else 'imported', game_dir=str(output), imported_files=result['files'], current_file='',
                            message='Disc verified. The experimental native build is ready to start.' if ready else
                                    'Disc imported. A built native executable is required to start the game.')
            except Exception as error:
                self.update(phase='error', error=str(error), message='Import stopped. Partial files are retained in your local library.')
            finally:
                with self.lock:
                    self.busy = False
        threading.Thread(target=run, daemon=True).start()

    def verify(self):
        if self.game is None:
            raise ValueError('Import your game disc first')
        self.begin('verifying', 'Checking your local game files.')

        def run():
            try:
                self.native_validate(self.game)
                self.update(phase='ready', progress=100, message='Local files verified. Ready for the experimental native build.')
            except Exception as error:
                self.update(phase='error', message='Verification stopped.', error=str(error))
            finally:
                with self.lock:
                    self.busy = False
        threading.Thread(target=run, daemon=True).start()

    def launch(self, keyboard=True):
        if self.game is None:
            raise ValueError('Import your game disc first')
        with self.lock:
            if self.busy or self.data['running'] or self.data['phase'] != 'ready':
                raise ValueError('Verify your game files before starting')
            self.busy = True
        try:
            self.native_validate(self.game)
            session = self.workspace / 'sessions' / (time.strftime('%Y%m%d-%H%M%S-') + uuid.uuid4().hex[:6])
            session.mkdir(parents=True)
            # Saves belong to this launcher's dedicated profile, never an
            # installed Xbox/emulator campaign or the import directory.
            saves = self.workspace / 'player-data'
            saves.mkdir(exist_ok=True)
            log_path = session / 'game.log'
            env = dict(os.environ, RECOMP_SAVE_DIR=str(saves), RECOMP_PB_EXEC='1', RECOMP_FB_WINDOW='1',
                       RECOMP_USB='1', RECOMP_VBLANK='1', RECOMP_UNIMPL_TRAP='1', RECOMP_STUB_LOG='1')
            # Development watchdogs/scripted inputs must not leak into a
            # player's launch. Explicit test launches use boot_test.py.
            for key in ('RECOMP_WATCHDOG_SECS', 'RECOMP_PAD_SCRIPT', 'BF_TRACE_DELAY', 'BF_SAMPLE_BOOT'):
                env.pop(key, None)
            for key in ('RECOMP_KEYBOARD', 'RECOMP_MKB'):
                env.pop(key, None)
            if keyboard:
                env['RECOMP_KEYBOARD'] = '1'
            with log_path.open('wb') as log:
                self.process = subprocess.Popen([str(self.executable), str(self.game), str(saves)],
                                                cwd=session, env=env, stdout=log, stderr=subprocess.STDOUT)
            self.update(phase='running', running=True, message='Native host running. Close its window to return here.', log=str(log_path))

            def monitor():
                code = self.process.wait()
                with log_path.open('rb') as log:
                    log.seek(max(0, log_path.stat().st_size - 8192))
                    tail = log.read().decode('utf-8', errors='replace')
                failed = code != 0 or '[EXIT] HalReturnToFirmware' in tail
                self.update(phase='error' if failed else 'ready', running=False,
                            message=f'Native host exited with code {code}.' if failed else 'Native host closed. Ready to return to your squad.',
                            error=tail[-2000:] if failed else '')
            threading.Thread(target=monitor, daemon=True).start()
        finally:
            with self.lock:
                self.busy = False
