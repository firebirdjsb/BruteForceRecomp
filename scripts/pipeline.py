"""Pinned, isolated static-recompilation pipeline. Never launches game code."""
import argparse
import json
import subprocess
import sys
from pathlib import Path
from verify_offsets import verify

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / 'toolchain.json').read_text(encoding='utf-8'))


def run(args, cwd, log):
    with log.open('w', encoding='utf-8') as output:
        subprocess.run([str(a) for a in args], cwd=cwd, stdout=output,
                       stderr=subprocess.STDOUT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=['verify', 'analyze', 'generate', 'runtime', 'build'])
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, default=ROOT / '.local')
    parser.add_argument('--toolkit', type=Path)
    parser.add_argument('--configuration', choices=['Release', 'Debug', 'RelWithDebInfo'], default='Release')
    args = parser.parse_args()
    xbe, work = args.xbe.resolve(), args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    try:
        result = verify(xbe)
        (work / 'verification.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        if args.stage == 'verify':
            print('Supported XBE verified; no game code executed.')
            return
        toolkit = args.toolkit.resolve() if args.toolkit else work / 'xboxrecomp'
        if not toolkit.exists():
            run(['git', 'clone', LOCK['repository'], toolkit], work, work / 'clone.log')
            run(['git', 'checkout', '--detach', LOCK['commit']], toolkit, work / 'checkout.log')
        commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=toolkit, text=True).strip()
        dirty = subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=toolkit, text=True)
        if commit != LOCK['commit'] or dirty.strip():
            raise ValueError('Toolkit must match the clean pinned commit; use a separate clone')
        import capstone
        if capstone.__version__ != LOCK['capstone']:
            raise ValueError('Install capstone==' + LOCK['capstone'] + ' in your Python environment')
        (work / 'provenance.json').write_text(json.dumps({**LOCK, 'python': sys.version}, indent=2), encoding='utf-8')
        if args.stage == 'build':
            if not (work / 'generation-audit.json').exists():
                raise ValueError('Generate and audit source before building')
            run(['cmake', '-S', ROOT, '-B', work / 'game-build',
                 '-DXBOXRECOMP_DIR=' + str(toolkit),
                 '-DBF_RECOVERED_DIR=' + str(work / 'recovered'),
                 '-DBF_GENERATED_DIR=' + str(work / 'generated')], work, work / 'game-configure.log')
            run(['cmake', '--build', work / 'game-build', '--config', args.configuration, '--parallel', '4'],
                work, work / 'game-build.log')
            print('Experimental host linked; gameplay remains unverified.')
            return
        def module(name, *options):
            print(name, flush=True)
            run([sys.executable, '-u', '-m', 'tools.' + name, xbe, *options],
                toolkit, work / (name + '.log'))
        if args.stage == 'runtime':
            run(['cmake', '-S', toolkit, '-B', work / 'runtime-build'], work, work / 'configure.log')
            run(['cmake', '--build', work / 'runtime-build', '--config', 'Release', '--parallel', '4'], work, work / 'runtime.log')
            print('Runtime libraries built; this is not a game executable.')
            return
        disasm, identified, abi = work / 'disasm', work / 'func_id', work / 'abi'
        module('xbe_parser', '--json', work / 'xbe_analysis.json', '--quiet')
        module('disasm', '--analysis-json', work / 'xbe_analysis.json', '-o', disasm, '-v')
        module('func_id', '--functions', disasm / 'functions.json', '--strings', disasm / 'strings.json',
               '--xrefs', disasm / 'xrefs.json', '-o', identified)
        module('abi_analysis', '--disasm-dir', disasm, '--func-id-dir', identified, '--output-dir', abi)
        if args.stage == 'generate':
            module('recomp', '--all', '--split', '100', '--game-name', 'Brute Force',
                   '--disasm-dir', disasm, '--func-id-dir', identified, '--abi-dir', abi,
                   '--gen-dir', work / 'generated', '-o', work / 'recomp-summary')
            from audit_generated import audit
            from prepare_native_media import prepare
            prepare(xbe, work / 'generated')
            from recover_entries import recover
            recover(xbe, toolkit, disasm / 'functions.json', work / 'recovered')
            report = audit(work / 'generated')
            print(f"Translation gaps: {report['unresolved_direct_count']} unresolved direct targets; "
                  f"{report['unsupported_markers']} unsupported instruction markers.")
        print('Stage complete. Analysis and generated code remain private in the work directory.')
    except (ValueError, OSError, subprocess.CalledProcessError, ImportError) as error:
        parser.exit(1, f'Pipeline stopped: {error}. Inspect logs in {work}\n')


if __name__ == '__main__':
    main()
