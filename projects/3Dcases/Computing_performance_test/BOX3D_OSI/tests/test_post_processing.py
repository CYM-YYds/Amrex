"""参数化后处理的数据契约回归，不调用 ParaView 或求解器。"""

import csv
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

POST = Path(__file__).resolve().parents[1] / "data_post_processing"
sys.path.insert(0, str(POST))
from convergence_logs import read_records
from plot_centerline import normalize_profile, read_profile, reference_errors
from plot_convergence import read_csv_records, read_plotfile_records, read_velocity


class PostProcessingTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="box3d-post-test-")
        self.root = Path(self.scratch.name)

    def tearDown(self):
        self.scratch.cleanup()

    def write_csv(self, name, header, rows):
        path = self.root / name
        with path.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(header)
            writer.writerows(rows)
        return path

    def test_masked_endpoints_and_internal_gap(self):
        header = ["Points:2", "ux", "vtkValidPointMask"]
        path = self.write_csv("sample.csv", header, [(0, 99, 0), (0.25, 1, 1), (0.75, 3, 1), (1, 99, 0)])
        points, invalid = read_profile(path)
        np.testing.assert_array_equal(points, [[0.25, 1], [0.75, 3]])
        self.assertEqual(invalid, 2)
        self.write_csv("sample.csv", header, [(0, 0, 1), (0.5, 0, 0), (1, 0, 1)])
        with self.assertRaisesRegex(ValueError, "内部存在无效采样"):
            read_profile(path)

    def test_unmasked_requires_explicit_permission_and_rejects_nonfinite(self):
        path = self.write_csv("legacy.csv", ["Points_2", "ux"], [(0, 0), (1, 1)])
        with self.assertRaises(ValueError):
            read_profile(path)
        points, invalid = read_profile(path, require_mask=False)
        np.testing.assert_array_equal(points, [[0, 0], [1, 1]])
        self.assertEqual(invalid, 0)
        self.write_csv("legacy.csv", ["Points_2", "ux"], [(0, "nan"), (1, 1)])
        with self.assertRaisesRegex(ValueError, "非有限"):
            read_profile(path, require_mask=False)

    def test_origin_scales_and_duplicate_coordinate_precision(self):
        points = normalize_profile(np.array([[10, 0.5], [12, 1.0]]), 2, 0.5, 10)
        np.testing.assert_array_equal(points, [[0, 1], [1, 2]])
        with self.assertRaises(ValueError):
            normalize_profile(points, 0, 0.5)
        path = self.write_csv("duplicates.csv", ["Points_Z", "ux"], [(0, 0), (0.50000001, 1), (0.50000002, 3), (1, 0)])
        aggregated, _ = read_profile(path, require_mask=False, round_digits=6)
        np.testing.assert_array_equal(aggregated, [[0, 0], [0.5, 2], [1, 0]])

    def test_reference_error_range_policy(self):
        simulation = np.array([[0, 0], [0.5, 1], [1, 0]])
        reference = np.array([[-0.1, 0], [0.25, 0.4], [1.1, 0]])
        with self.assertRaisesRegex(ValueError, "超出"):
            reference_errors(simulation, reference)
        metrics = reference_errors(simulation, reference, outside="exclude")
        self.assertEqual((metrics["n_used"], metrics["n_excluded"]), (1, 2))
        for key in ["mae", "rmse", "max_abs_error"]:
            self.assertAlmostEqual(metrics[key], 0.1)

    def test_restart_log_precedence_and_strict_fields(self):
        def records(value):
            return (f"CONVERGENCE_TREND step=3200 interval=1000 velocity_l2_relative={value} velocity_l2=2 delta_velocity_l2=1\n"
                    f"CONVERGENCE step=3200 interval=3200 velocity_l2_relative={value} velocity_l2=2 delta_velocity_l2=1 tolerance=0.1 consecutive=0/3 converged=0\n")
        first, second = self.root / "first.log", self.root / "second.log"
        first.write_text(records(0.5))
        second.write_text(records(0.25))
        trend, formal = read_records([first, second])
        self.assertEqual((trend[0][2], formal[0][2]), (0.25, 0.25))
        second.write_text("CONVERGENCE_TREND step=6400 interval=1000\n")
        with self.assertRaisesRegex(ValueError, "incomplete"):
            read_records([first, second])

    def test_csv_does_not_invent_formal_state(self):
        header = ["step", "interval", "velocity_l2_relative", "velocity_l2", "delta_velocity_l2"]
        path = self.write_csv("trend.csv", header, [(6400, 3200, 0.1, 2, 0.2)])
        trend, formal = read_csv_records([path])
        self.assertEqual(trend, [(6400, 3200, 0.1, 2.0, 0.2)])
        self.assertEqual(formal, [])
        path = self.write_csv("formal.csv", ["record", *header], [("trend", 6400, 1000, 0.1, 2, 0.2), ("formal", 6400, 3200, 0.2, 2, 0.4)])
        _, formal = read_csv_records([path])
        self.assertEqual(len(formal[0]), 5)
        result = subprocess.run([sys.executable, str(POST / "plot_convergence.py"), "--csv", str(path),
                                 "--output", str(self.root / "missing_tolerance.png")], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertFalse((self.root / "missing_tolerance.png").exists())

    def test_plotfile_formula_interval_and_format_guard(self):
        for step in [3200, 6400]:
            (self.root / f"plt_{step}").mkdir()
        with patch("plot_convergence.read_velocity", side_effect=[np.array([1.0, 2.0]), np.array([2.0, 4.0])]):
            trend, formal = read_plotfile_records(self.root)
        self.assertAlmostEqual(trend[0][2], 0.5)
        self.assertEqual(formal, [])
        (self.root / "plt_6400").rename(self.root / "plt_9600")
        with patch("plot_convergence.read_velocity", side_effect=[np.ones(2), np.ones(2)]):
            with self.assertRaisesRegex(ValueError, "连续 3200"):
                read_plotfile_records(self.root)
        (self.root / "plt_3200/Header").write_text("wrong format\n")
        with self.assertRaisesRegex(ValueError, "变量或维度"):
            read_velocity(self.root / "plt_3200")

    def test_cli_cannot_overwrite_input(self):
        path = self.write_csv("input.csv", ["Points_2", "ux"], [(0, 0), (1, 1)])
        before = path.read_bytes()
        result = subprocess.run([sys.executable, str(POST / "plot_centerline.py"), "--profile", str(path), "sample", "1",
                                 "--height", "1", "--lid-velocity", "1", "--allow-unmasked",
                                 "--output", str(path.with_suffix(".png"))], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(path.read_bytes(), before)
        self.assertFalse(path.with_suffix(".png").exists())


if __name__ == "__main__":
    unittest.main()
