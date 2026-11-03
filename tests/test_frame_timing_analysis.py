import csv
import importlib.util
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location("timing", Path(__file__).parents[1] / "scripts/analyze_frame_timing.py")
timing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timing)


def fixture(tmp_path, speed=1, distinct=1):
    frames, clock = tmp_path / "frames.csv", tmp_path / "clock.csv"
    with frames.open("w", newline="") as out:
        writer = csv.writer(out)
        writer.writerow(["t_us", "distinct"])
        writer.writerows((round(i * 1_000_000 / 60), distinct) for i in range(601))
    with clock.open("w", newline="") as out:
        writer = csv.writer(out)
        writer.writerow(["host_us", "world", "target", "minimum"])
        writer.writerows((i * 100_000, "123400", i * .1 * speed, 1 / 66) for i in range(101))
    return frames, clock


def test_reports_doubled_simulation_despite_60_fps(tmp_path):
    result = timing.analyze(*fixture(tmp_path, speed=2))
    assert result["presented_fps"] == pytest.approx(60)
    assert result["physics_clocks"][0]["game_seconds_per_wall_second"] == pytest.approx(2)
    assert result["manual_gameplay_verified"] is False


def test_repeated_pictures_are_not_claimed_as_new_frames(tmp_path):
    result = timing.analyze(*fixture(tmp_path, distinct=0))
    assert result["presented_fps"] == pytest.approx(60)
    assert result["distinct_picture_fraction"] == 0


def test_original_physics_step_remains_visible(tmp_path):
    result = timing.analyze(*fixture(tmp_path))
    assert result["physics_clocks"][0]["minimum_step_min"] == pytest.approx(1 / 66)
    assert result["physics_clocks"][0]["game_seconds_per_wall_second"] == pytest.approx(1)


def test_unavailable_pixels_remain_unknown(tmp_path):
    result = timing.analyze(*fixture(tmp_path, distinct=-1))
    assert result["distinct_picture_fraction"] is None


def test_empty_frame_capture_is_rejected(tmp_path):
    frames, clock = fixture(tmp_path)
    frames.write_text("t_us,distinct\n")
    with pytest.raises(ValueError):
        timing.analyze(frames, clock)


def test_normal_build_does_not_invent_physics_measurements(tmp_path):
    frames, _ = fixture(tmp_path)
    result = timing.analyze(frames)
    assert result["presented_fps"] == pytest.approx(60)
    assert result["physics_clocks"] == []
    assert result["manual_gameplay_verified"] is False
