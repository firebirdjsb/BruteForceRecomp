"""Generate an ignored fixture from authenticated local helpers and an owned XBE.

No translated retail bodies or retail constant bytes are stored in this script.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from verify_offsets import verify


EXPECTED = {
    "recomp_0022.c": "7a8ab670472c875ffb5f546105979b24e47853823b6619d9566764785f96beee",
    "recomp_0023.c": "fb2a07684f5526f8277e1d07e2215902b110944d5434f4e0f91b64b3732180a1",
}


def prepare(generated, xbe, output):
    repository = Path(__file__).resolve().parents[1]
    output = output.resolve()
    # Any retail-derived output inside this checkout must be ignored. External
    # build directories cannot be added to this repository's tracked sources.
    if output.is_relative_to(repository):
        ignored = subprocess.run(["git", "-C", str(repository), "check-ignore", "--quiet", "--no-index", "--",
                                  str(output / "native_aspect_original_helpers.inc")], check=False)
        if ignored.returncode != 0:
            raise ValueError("Retail helper fixture output must be inside an ignored build directory")
    validated = verify(xbe)
    sources = {}
    for name, digest in EXPECTED.items():
        data = (generated / name).read_bytes().replace(b"\r\n", b"\n")
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError(f"Unaudited original helper source: {name}")
        sources[name] = data.decode("utf-8")
    functions = []
    for name, address in [("recomp_0022.c", "0006AD00"), ("recomp_0022.c", "0006AD90"),
                          ("recomp_0023.c", "0006C840")]:
        source = sources[name]
        begin = source.index(f"void sub_{address}(void)")
        end = source.index("/**", begin)
        functions.append(source[begin:end].rstrip() + "\n")
    helpers = "\n".join(functions)
    image = xbe.read_bytes()
    if hashlib.sha256(image).hexdigest() != validated["sha256"]:
        raise ValueError("Owned XBE changed after verification")
    dword = lambda offset: struct.unpack_from("<I", image, offset)[0]
    count, table = dword(0x11C), dword(0x120) - dword(0x104)
    sections = []
    for index in range(count):
        flags, va, virtual_size, raw, raw_size = struct.unpack_from("<5I", image, table + index * 56)
        sections.append((va, raw, raw_size))
    constants = {}
    for kind, address in re.findall(r"MEM([FD])\(0x([0-9A-Fa-f]+)\)", helpers):
        va, size = int(address, 16), 8 if kind == "D" else 4
        matches = [(start, raw) for start, raw, length in sections if start <= va and va + size <= start + length]
        if len(matches) != 1:
            raise ValueError(f"Original helper constant {va:08X} is not uniquely backed by retail data")
        start, raw = matches[0]
        constants[va] = image[raw + va - start:raw + va - start + size]
    header = ["/* Ignored build output: authenticated original translated helpers. */", helpers,
              "static void bf_native_aspect_fixture_load_constants(unsigned char *arena) {"]
    for va, data in sorted(constants.items()):
        header.append("    { static const unsigned char value[] = {" + ", ".join(f"0x{x:02X}" for x in data) +
                      f"}}; memcpy(arena+0x{va:08X}u,value,sizeof(value)); }}")
    header.append("}\n")
    output.mkdir(parents=True, exist_ok=True)
    (output / "native_aspect_original_helpers.inc").write_text("\n".join(header), encoding="utf-8", newline="\n")
    provenance = {"owned_xbe_sha256": validated["sha256"], "generated_lf_sha256": EXPECTED,
                  "original_helper_addresses": ["0006AD00", "0006AD90", "0006C840"],
                  "constant_addresses": [f"{va:08X}" for va in sorted(constants)],
                  "renderer_called_by_fixture": False}
    (output / "native-aspect-fixture-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated-dir", type=Path, required=True)
    parser.add_argument("--xbe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        prepare(args.generated_dir, args.xbe, args.output)
    except (ValueError, OSError, struct.error) as error:
        parser.exit(1, f"Native aspect fixture preparation failed: {error}\n")


if __name__ == "__main__":
    main()
