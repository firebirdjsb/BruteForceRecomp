"""Bounded diagnostic boot with exact input validation and disposable saves."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import sys
from verify_offsets import verify


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=15)
    parser.add_argument('--gpu-trace', action='store_true')
    parser.add_argument('--keyboard', action='store_true', help='Expose the runtime keyboard as controller port 0')
    parser.add_argument('--mouse-keyboard', action='store_true', help='Exercise the runtime raw-mouse/keyboard adapter; gameplay behavior requires QA')
    parser.add_argument('--sample-startup', action='store_true', help='Sample the Windows host main thread')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 60:
        parser.error('--seconds must be between 1 and 60')
    try:
        game = args.game_dir.resolve()
        verify(game / 'default.xbe')
        work = args.work_dir.resolve()
        work.mkdir(parents=True, exist_ok=True)
        scratch = Path(tempfile.mkdtemp(prefix='boot-', dir=work))
        env = dict(os.environ, RECOMP_SAVE_DIR=str(scratch / 'saves'),
                   RECOMP_UNIMPL_TRAP='1', RECOMP_STUB_LOG='1',
                   BF_TRACE_BOOT='1',
                   RECOMP_PB_EXEC='1', RECOMP_FB_WINDOW='1',
                   RECOMP_USB='1', RECOMP_VBLANK='1',
                   RECOMP_WATCHDOG_SECS=str(max(1, args.seconds - 2)))
        if args.gpu_trace:
            env['RECOMP_NV2A_TRACE'] = '1'
        if args.keyboard or args.mouse_keyboard:
            env['RECOMP_KEYBOARD'] = '1'
        if args.mouse_keyboard:
            env['RECOMP_MKB'] = '1'
        if args.sample_startup:
            env['BF_SAMPLE_BOOT'] = '1'
        with (scratch / 'boot.log').open('w', encoding='utf-8') as log:
            process = subprocess.Popen([str(args.exe.resolve()), str(game), str(scratch / 'saves')],
                                       cwd=scratch, env=env, stdout=log, stderr=subprocess.STDOUT)
            timed_out = False
            try:
                code = process.wait(timeout=args.seconds)
            except subprocess.TimeoutExpired:
                timed_out = True
                process.kill()
                code = process.wait()
        text = (scratch / 'boot.log').read_text(encoding='utf-8', errors='replace')
        firmware_exit = '[EXIT] HalReturnToFirmware' in text
        watchdog = '[WATCHDOG]' in text
        unsupported = 'Unsupported instruction at' in text
        result = {'returncode': code, 'time_limit_reached': timed_out,
                  'startup_requested_firmware_return': firmware_exit,
                  'watchdog_expired': watchdog,
                  'status': ('EARLY_FIRMWARE_RETURN' if firmware_exit else
                             'BOUNDED_RUN_ENDED' if watchdog or timed_out else
                             'UNSUPPORTED_INSTRUCTION' if unsupported else 'DIAGNOSTIC_ONLY'),
                  'manual_gameplay_verified': False, 'log': str(scratch / 'boot.log')}
        (scratch / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(json.dumps(result, indent=2))
        if code != 0 or firmware_exit or watchdog or timed_out:
            sys.exit(1)
    except (OSError, ValueError) as error:
        parser.exit(1, f'Boot test stopped: {error}\n')


if __name__ == '__main__':
    main()
