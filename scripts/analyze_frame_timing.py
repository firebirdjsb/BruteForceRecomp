"""Compare actual NV2A flips with the retail physics clock; never infer gameplay QA."""
import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path


def analyze(frames_path, clock_path=None, window=25.0):
    with Path(frames_path).open(newline="", encoding="utf-8") as source:
        frames = list(csv.DictReader(source))
    if len(frames) < 2 or window < 5 or not math.isfinite(window):
        raise ValueError("Need at least two frames and a finite window of at least five seconds")
    end = int(frames[-1]["t_us"])
    start = end - window * 1_000_000
    frames = [row for row in frames if start <= int(row["t_us"]) <= end]
    times = [int(row["t_us"]) for row in frames]
    if len(times) < 2 or any(b <= a for a, b in zip(times, times[1:])):
        raise ValueError("Frame timestamps must increase strictly")
    seconds = (times[-1] - times[0]) / 1_000_000
    if seconds < 5:
        raise ValueError("The selected frame window is shorter than five seconds")
    groups = defaultdict(list)
    if clock_path is not None:
        with Path(clock_path).open(newline="", encoding="utf-8") as source:
            for row in csv.DictReader(source):
                if times[0] <= int(row["host_us"]) <= times[-1]:
                    groups[row["world"]].append(row)
    clocks = []
    for world, rows in groups.items():
        rows.sort(key=lambda row: int(row["host_us"]))
        origin = int(rows[0]["host_us"])
        x = [(int(row["host_us"]) - origin) / 1_000_000 for row in rows]
        y = [float(row["target"]) for row in rows]
        minimums = [float(row["minimum"]) for row in rows]
        if len(rows) < 3 or x[-1] < 5:
            continue
        if any(not math.isfinite(value) for value in y + minimums):
            raise ValueError("Non-finite physics timing data")
        # Reset/pause/loading windows cannot establish normal gameplay speed.
        # Fit the complete selected window, with its span included in output.
        mean_x, mean_y = sum(x) / len(x), sum(y) / len(y)
        slope = sum((a - mean_x) * (b - mean_y) for a, b in zip(x, y)) / sum(
            (a - mean_x) ** 2 for a in x)
        clocks.append({"world": world, "samples": len(rows), "span_seconds": x[-1],
                       "game_seconds_per_wall_second": slope,
                       "minimum_step_min": min(minimums), "minimum_step_max": max(minimums)})
    gaps = sorted((b - a) / 1000 for a, b in zip(times, times[1:]))
    known = [row for row in frames if int(row["distinct"]) >= 0]
    distinct = sum(int(row["distinct"]) == 1 for row in known)
    return {"window_seconds": seconds, "flips": len(frames),
            "presented_fps": (len(frames) - 1) / seconds,
            "frame_gap_p95_ms": gaps[min(len(gaps) - 1, math.ceil(len(gaps) * .95) - 1)],
            "known_picture_hashes": len(known),
            "distinct_picture_fraction": distinct / len(known) if known else None,
            "physics_clocks": sorted(clocks, key=lambda item: -item["samples"]),
            "manual_gameplay_verified": False,
            "limitation": "Clock and flip telemetry cannot establish movement, animation, mission, audio or multiplayer correctness."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=Path, required=True)
    parser.add_argument("--clock", type=Path, help="Optional hash-gated diagnostic clock capture")
    parser.add_argument("--window", type=float, default=25)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    try:
        result = analyze(args.frames, args.clock, args.window)
        output = json.dumps(result, indent=2) + "\n"
        if args.json:
            args.json.write_text(output, encoding="utf-8")
        print(output, end="")
    except (OSError, ValueError, KeyError, ZeroDivisionError) as error:
        parser.exit(1, f"Timing analysis failed: {error}\n")


if __name__ == "__main__":
    main()
