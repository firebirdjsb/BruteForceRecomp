#!/usr/bin/env python3
"""Summarize opt-in NV2A frame CSV files without confusing flips with new images.

Sources: RECOMP_FRAME_STATS CSV from the audited pinned NV2A runtime.
A high flip rate is not evidence of high rendered-world frame rate.
Unknown hashes are reported, never counted as new or duplicate frames.
No automatically inferred scene/viewport/input equivalence.
"""
import argparse
import csv
import json
from pathlib import Path

REQUIRED = ("flip", "t_us", "draws", "program_vertices", "hash", "distinct", "constants")


def summarize(path):
    with open(path, "r", encoding="utf-8-sig", newline="") as stream:
        rows = csv.DictReader(stream)
        if rows.fieldnames is None or not set(REQUIRED).issubset(rows.fieldnames):
            raise ValueError(f"{path}: missing required RECOMP_FRAME_STATS columns")
        data = []
        for number, row in enumerate(rows, 2):
            try:
                values = {k: int(row[k]) for k in
                          ("flip", "t_us", "draws", "program_vertices", "distinct")}
                values["hash"] = int(row["hash"], 16)
                values["constants"] = int(row["constants"], 16)
            except (ValueError, TypeError, KeyError) as exc:
                raise ValueError(f"{path}:{number}: invalid frame row") from exc
            if values["flip"] <= 0 or values["t_us"] < 0 or any(
                    values[k] < 0 for k in ("draws", "program_vertices")):
                raise ValueError(f"{path}:{number}: negative or invalid counters")
            if values["distinct"] not in (-1, 0, 1) or (
                    (values["hash"] == 0) != (values["distinct"] == -1)):
                raise ValueError(f"{path}:{number}: hash and distinct status disagree")
            if data and (values["flip"] != data[-1]["flip"] + 1 or
                         values["t_us"] < data[-1]["t_us"]):
                raise ValueError(f"{path}:{number}: flip sequence/time out of order")
            data.append(values)
    if len(data) < 2 or data[-1]["t_us"] <= data[0]["t_us"]:
        raise ValueError(f"{path}: need >=2 flips and a positive elapsed duration")
    elapsed = (data[-1]["t_us"] - data[0]["t_us"]) / 1_000_000
    interval = data[1:]   # first flip is a boundary, not an observed interval
    visible = sum(x["distinct"] == 1 for x in interval)
    unknown = sum(x["distinct"] == -1 for x in interval)
    return {
        "source": str(path),
        "duration_seconds": round(elapsed, 6),
        "flip_intervals": len(interval),
        "flip_hz": round(len(interval) / elapsed, 3),
        "distinct_image_intervals": visible,
        "unknown_image_intervals": unknown,
        "distinct_image_hz_lower_bound": round(visible / elapsed, 3),
        "draws": sum(x["draws"] for x in interval),
        "program_vertices": sum(x["program_vertices"] for x in interval),
        "warning": "Image hashes do not prove world progress or independently matched scenes.",
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="+", type=Path, help="one or more NV2A CSV traces")
    parser.add_argument("--out", type=Path, help="optional output JSON file")
    args = parser.parse_args(argv)
    try:
        result = {"captures": [summarize(path) for path in args.captures],
                  "comparison_validated": False,
                  "note": "Rates are descriptive only. Do not compare as an FPS speedup without independently reviewed matching input/state and matched time ranges."}
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    serialized = json.dumps(result, indent=2)
    if args.out is None:
        print(serialized)
    else:
        args.out.write_text(serialized + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
