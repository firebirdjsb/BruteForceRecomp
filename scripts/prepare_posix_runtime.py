"""Prepare narrowly patched private runtime sources for native POSIX MMIO."""
import argparse
import hashlib
from pathlib import Path

SOURCES = {
    'kernel/xbox_memory_layout.c': '9b62ad9fc0f1c6061faea61f71d2a58ee01d27b3e487e01601e8d44a251cea3d',
    'apu/apu_mmio_hook.c': 'e28e79707e3be2cfcdce7f3d809e06d47e67c1b3482be2f2a022b883d1b57ee4',
    'usb/ohci.c': 'df8e1c265db219b58c4d3e632d694d9da5fc81ff99d636dc2cb176e03ab02ea9',
}


def prepare(toolkit, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    prepared = []
    for name, expected in SOURCES.items():
        source = (Path(toolkit) / 'src' / name).read_text(encoding='utf-8')
        if hashlib.sha256(source.encode()).hexdigest() != expected:
            raise ValueError(f'Unaudited runtime source: {name}')
        # The new public x64 decoder and original ARM64 decoder are selected
        # by architecture. Keep the actual device callbacks and address tests.
        # Keep upstream feature-test macros (_GNU_SOURCE) ahead of every
        # system header, including ucontext pulled in by the decoder.
        first_include = source.index('#include ')
        source = source[:first_include] + '#include "bf_mmio_host.h"\n' + source[first_include:]
        source = source.replace('#if !defined(_WIN32) && defined(__aarch64__)',
                                '#if !defined(_WIN32) && defined(MMIO_POSIX_TRAP)')
        source = source.replace('#if !defined(_WIN32) && !defined(__aarch64__)',
                                '#if !defined(_WIN32) && !defined(MMIO_POSIX_TRAP)')
        source = source.replace('#if defined(__aarch64__)\n', '#if defined(MMIO_POSIX_TRAP)\n')
        source = source.replace('#elif defined(__aarch64__)\n', '#elif defined(MMIO_POSIX_TRAP)\n')
        source = source.replace('mmio_emulate_a64(', 'mmio_emulate_host(')
        source = source.replace('A64_CTX_PC(uc)', 'MMIO_HOST_PC(uc)')
        if name == 'kernel/xbox_memory_layout.c':
            # A RAM+mirrors reservation can land at the top of Linux's user
            # address space. Adding guest 0x80000000 then crosses its limit;
            # contiguous/GPU/MMIO mappings fail and the first allocation dies.
            # Reserve the full 32-bit *virtual* window. Existing carve-out
            # helpers commit only the actual RAM/device views. This does not
            # enable the optional extended guest allocator.
            before = ('        if (ext_vma_span(g_span_size)) {\n'
                      '            g_span_size = (size_t)1 << 32;\n'
                      '            g_span_whole_window = 1;\n'
                      '        }')
            if source.count(before) != 1:
                raise ValueError('Expected POSIX guest-window reservation was not found')
            source = source.replace(before,
                '        /* Brute Force POSIX: keep every guest aperture inside our reserved window. */\n'
                '        g_span_size = (size_t)1 << 32;\n'
                '        g_span_whole_window = 1;')
        # Existing relative includes still resolve through the original
        # source include directory. The new decoder's header guard prevents
        # the old Windows-only header from being included a second time.
        target = output / Path(name).name
        if not target.exists() or target.read_text(encoding='utf-8') != source:
            target.write_text(source, encoding='utf-8', newline='\n')
        prepared.append(target)
    return prepared


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolkit', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.toolkit, args.output)
