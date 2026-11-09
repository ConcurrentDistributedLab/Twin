"""
test_benchmark_plotter.py

Tests for data_loader, file_utils, plot_engine, and debug_logger.

Run with:
    python test_benchmark_plotter.py            # colored output, no dependencies
    python -m pytest test_benchmark_plotter.py -v   # if pytest is installed
"""

import json
import os
import sys
import math
import unittest
import unittest.mock
import tempfile
import shutil
import time
import numpy as np
import pandas as pd

sys.path.insert(0, os.path.dirname(__file__))

import debug_logger
from data_loader import parse_key_value_list, parse_marker_list, load_data_as_dataframe
from file_utils import get_unique_base_dir, ensure_subdirectories
from plot_engine import calculate_x_positions, aggregate_metric
from main import resolve_exclude_names, apply_plot_exclude


# ===========================================================================
# Colored output runner
# ===========================================================================

GREEN  = "\033[92m"
RED    = "\033[91m"
YELLOW = "\033[93m"
CYAN   = "\033[96m"
BOLD   = "\033[1m"
RESET  = "\033[0m"
DIM    = "\033[2m"

class ColorResult(unittest.TestResult):
    """Custom TestResult that prints one green/red line per test."""

    def __init__(self, stream=None, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.stream = stream or sys.stdout
        self._start_times = {}
        self._results = []   # list of (status, test, elapsed, msg)

    def startTest(self, test):
        super().startTest(test)
        self._start_times[test] = time.perf_counter()

    def _elapsed(self, test):
        return time.perf_counter() - self._start_times.get(test, 0)

    def addSuccess(self, test):
        super().addSuccess(test)
        self._results.append(("ok", test, self._elapsed(test), None))

    def addFailure(self, test, err):
        super().addFailure(test, err)
        msg = self._exc_info_to_string(err, test).strip().splitlines()[-1]
        self._results.append(("FAIL", test, self._elapsed(test), msg))

    def addError(self, test, err):
        super().addError(test, err)
        msg = self._exc_info_to_string(err, test).strip().splitlines()[-1]
        self._results.append(("ERROR", test, self._elapsed(test), msg))

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self._results.append(("SKIP", test, self._elapsed(test), reason))


class ColorRunner:
    """Runs a test suite and prints a formatted, colored report."""

    SEP = "-" * 72

    def run(self, suite):
        result = ColorResult()

        # Collect tests grouped by class
        all_tests = list(suite)
        flat = []
        for item in all_tests:
            if hasattr(item, "__iter__"):
                flat.extend(item)
            else:
                flat.append(item)

        # Re-flatten one more level (unittest loads nested suites)
        def _flatten(s):
            for t in s:
                if isinstance(t, unittest.TestSuite):
                    yield from _flatten(t)
                else:
                    yield t

        flat = list(_flatten(suite))

        print(f"\n{BOLD}{CYAN}{'=' * 72}{RESET}")
        print(f"{BOLD}{CYAN}  BENCHMARK PLOTTER - TEST SUITE{RESET}")
        print(f"{BOLD}{CYAN}{'=' * 72}{RESET}\n")

        current_class = None
        result.startTestRun()

        for test in flat:
            cls_name = type(test).__name__
            if cls_name != current_class:
                if current_class is not None:
                    print()
                print(f"  {BOLD}{YELLOW}{cls_name}{RESET}")
                print(f"  {DIM}{self.SEP[2:]}{RESET}")
                current_class = cls_name

            # Run the single test
            test(result)
            status, _, elapsed, msg = result._results[-1]

            method = test._testMethodName
            # Friendly name: remove test_ prefix and replace _ with spaces
            friendly = method.replace("test_", "").replace("_", " ")

            elapsed_str = f"{DIM}({elapsed*1000:.0f}ms){RESET}"

            if status == "ok":
                tick = f"{GREEN}OK{RESET}"
                print(f"    {tick}  {friendly} {elapsed_str}")
            elif status == "FAIL":
                cross = f"{RED}X{RESET}"
                print(f"    {cross}  {RED}{friendly}{RESET} {elapsed_str}")
                print(f"       {DIM}-> {msg}{RESET}")
            elif status == "ERROR":
                cross = f"{RED}!{RESET}"
                print(f"    {cross}  {RED}{friendly} [ERROR]{RESET} {elapsed_str}")
                print(f"       {DIM}-> {msg}{RESET}")
            elif status == "SKIP":
                print(f"    {YELLOW}-  {friendly} [skipped]{RESET} {elapsed_str}")

        result.stopTestRun()

        # --- Summary ---
        total   = result.testsRun
        passed  = total - len(result.failures) - len(result.errors) - len(result.skipped)
        failed  = len(result.failures) + len(result.errors)
        skipped = len(result.skipped)

        print(f"\n  {DIM}{self.SEP}{RESET}")

        if failed == 0:
            badge = f"{GREEN}{BOLD} PASSED {RESET}"
            summary_color = GREEN
        else:
            badge = f"{RED}{BOLD} FAILED {RESET}"
            summary_color = RED

        print(f"\n  {badge}  "
              f"{summary_color}{BOLD}{passed}/{total}{RESET} tests passed"
              + (f"  {YELLOW}({skipped} skipped){RESET}" if skipped else "")
              + (f"  {RED}({failed} failed){RESET}" if failed else ""))

        # Print full failure details at the bottom
        if result.failures or result.errors:
            print(f"\n{BOLD}{RED}  Failure Details:{RESET}")
            for test, traceback in result.failures + result.errors:
                print(f"\n  {RED}X {type(test).__name__}.{test._testMethodName}{RESET}")
                for line in traceback.strip().splitlines():
                    print(f"    {DIM}{line}{RESET}")

        print()
        return result


# ===========================================================================
# Helpers
# ===========================================================================

def _make_jsonl(tmp_dir, records, filename="data.jsonl"):
    path = os.path.join(tmp_dir, filename)
    with open(path, "w") as f:
        for r in records:
            f.write(json.dumps(r) + "\n")
    return path


SAMPLE_RECORDS = [
    {"algorithm": "AlgA", "threads": 1, "metrics": {"result": 100.0, "time": 0.5}},
    {"algorithm": "AlgA", "threads": 2, "metrics": {"result": 180.0, "time": 0.3}},
    {"algorithm": "AlgB", "threads": 1, "metrics": {"result": 90.0,  "time": 0.6}},
    {"algorithm": "AlgB", "threads": 2, "metrics": {"result": 170.0, "time": 0.35}},
    {"algorithm": "AlgC", "threads": 1, "metrics": {"result": 50.0,  "time": 1.0}},
]

# Records that contain the hardware-counter columns used in the cache-miss expression
CACHE_RECORDS = [
    {
        "algorithm": "AlgA", "threads": 1,
        "metrics": {
            "L1_dcache_load_misses": 1000, "l2_lines_in_all": 500,
            "LLC_load_misses": 200,
            "total_inserts": 800, "total_deletes": 200,
            "result": 1.0,
        },
    },
    {
        "algorithm": "AlgA", "threads": 1,
        "metrics": {
            "L1_dcache_load_misses": 2000, "l2_lines_in_all": 1000,
            "LLC_load_misses": 400,
            "total_inserts": 1600, "total_deletes": 400,
            "result": 1.0,
        },
    },
    {
        "algorithm": "AlgB", "threads": 1,
        "metrics": {
            "L1_dcache_load_misses": 500,  "l2_lines_in_all": 250,
            "LLC_load_misses": 100,
            "total_inserts": 400,  "total_deletes": 100,
            "result": 1.0,
        },
    },
    # AlgA at threads=2 for multi-thread tests
    {
        "algorithm": "AlgA", "threads": 2,
        "metrics": {
            "L1_dcache_load_misses": 3000, "l2_lines_in_all": 1500,
            "LLC_load_misses": 600,
            "total_inserts": 2400, "total_deletes": 600,
            "result": 1.0,
        },
    },
    # Edge-case: zero denominator
    {
        "algorithm": "AlgZero", "threads": 1,
        "metrics": {
            "L1_dcache_load_misses": 100,  "l2_lines_in_all": 50,
            "LLC_load_misses": 25,
            "total_inserts": 0, "total_deletes": 0,
            "result": 1.0,
        },
    },
    # Edge-case: zero numerator
    {
        "algorithm": "AlgZeroNum", "threads": 1,
        "metrics": {
            "L1_dcache_load_misses": 0, "l2_lines_in_all": 0,
            "LLC_load_misses": 0,
            "total_inserts": 500, "total_deletes": 500,
            "result": 1.0,
        },
    },
]

# The production expression used for cache-miss ratio
CACHE_EXPR = (
    "expression: (`metrics.L1_dcache_load_misses` + `metrics.l2_lines_in_all` "
    "+ `metrics.LLC_load_misses`) / (`metrics.total_inserts` + `metrics.total_deletes`)"
)

BASE_PLOT_DEF = {
    "data_source": {
        "x_value": "threads",
        "y_value": "metrics.result",
        "aggregator": "mean",
        "trim_by": "metrics.result",
    },
    "trim_k": 0,
    "output_file": "out.png",
}


def _cache_plot_def(agg="mean", k=0):
    """Plot definition that uses the full cache-miss division expression."""
    return {
        "data_source": {
            "y_value": CACHE_EXPR,
            "aggregator": agg,
            "trim_by": "metrics.result",
        },
        "trim_k": k,
    }


# ===========================================================================
# debug_logger
# ===========================================================================

class TestDebugLogger(unittest.TestCase):

    def setUp(self):
        debug_logger.set_debug(False)

    def tearDown(self):
        debug_logger.set_debug(False)
        os.environ.pop("DEBUG", None)

    def test_debug_off_by_default(self):
        self.assertFalse(debug_logger.is_debug())

    def test_set_debug_true(self):
        debug_logger.set_debug(True)
        self.assertTrue(debug_logger.is_debug())

    def test_set_debug_false(self):
        debug_logger.set_debug(True)
        debug_logger.set_debug(False)
        self.assertFalse(debug_logger.is_debug())

    def test_dbg_prints_when_enabled(self):
        import io
        debug_logger.set_debug(True)
        buf = io.StringIO()
        with unittest.mock.patch("sys.stdout", buf):
            debug_logger.dbg("hello test")
        self.assertIn("hello test", buf.getvalue())

    def test_dbg_silent_when_disabled(self):
        import io
        buf = io.StringIO()
        with unittest.mock.patch("sys.stdout", buf):
            debug_logger.dbg("should not appear")
        self.assertEqual(buf.getvalue(), "")

    def test_env_variable_enables_debug(self):
        debug_logger.set_debug(False)
        os.environ["DEBUG"] = "1"
        self.assertTrue(debug_logger.is_debug())

    def test_dbg_dataframe_output(self):
        import io
        debug_logger.set_debug(True)
        df = pd.DataFrame({"a": [1, 2], "b": [3, 4]})
        buf = io.StringIO()
        with unittest.mock.patch("sys.stdout", buf):
            debug_logger.dbg_dataframe(df, "TestDF")
        out = buf.getvalue()
        self.assertIn("TestDF", out)
        self.assertIn("Shape", out)

    def test_dbg_dict_output(self):
        import io
        debug_logger.set_debug(True)
        buf = io.StringIO()
        with unittest.mock.patch("sys.stdout", buf):
            debug_logger.dbg_dict({"mykey": "myval"}, "MyDict")
        out = buf.getvalue()
        self.assertIn("MyDict", out)
        self.assertIn("mykey", out)


# ===========================================================================
# parse_key_value_list
# ===========================================================================

class TestParseKeyValueList(unittest.TestCase):

    def test_basic_colon(self):
        self.assertEqual(parse_key_value_list(["AlgA:red", "AlgB:blue"]), {"AlgA": "red", "AlgB": "blue"})

    def test_double_equals_separator(self):
        self.assertEqual(parse_key_value_list(["OldName==NewName"], separator="=="), {"OldName": "NewName"})

    def test_value_with_extra_separator(self):
        self.assertEqual(parse_key_value_list(["key:val:extra"]), {"key": "val:extra"})

    def test_empty_list(self):
        self.assertEqual(parse_key_value_list([]), {})

    def test_none(self):
        self.assertEqual(parse_key_value_list(None), {})

    def test_item_without_separator_skipped(self):
        result = parse_key_value_list(["no_sep", "good:item"])
        self.assertNotIn("no_sep", result)
        self.assertEqual(result["good"], "item")

    def test_whitespace_stripped(self):
        self.assertEqual(parse_key_value_list([" AlgA : red "]), {"AlgA": "red"})

    def test_multiple_renames(self):
        self.assertEqual(
            parse_key_value_list(["A==X", "B==Y", "C==Z"], separator="=="),
            {"A": "X", "B": "Y", "C": "Z"},
        )


# ===========================================================================
# parse_marker_list
# ===========================================================================

class TestParseMarkerList(unittest.TestCase):

    def test_marker_with_fillstyle(self):
        self.assertEqual(parse_marker_list(["AlgA:o:none"]), {"AlgA": {"marker": "o", "fillstyle": "none"}})

    def test_marker_without_fillstyle_defaults_full(self):
        result = parse_marker_list(["AlgB:s"])
        self.assertEqual(result["AlgB"]["fillstyle"], "full")
        self.assertEqual(result["AlgB"]["marker"], "s")

    def test_multiple_entries(self):
        result = parse_marker_list(["A:o:none", "B:^:full", "C:D"])
        self.assertEqual(len(result), 3)
        self.assertEqual(result["C"]["fillstyle"], "full")

    def test_empty_list(self):
        self.assertEqual(parse_marker_list([]), {})

    def test_none(self):
        self.assertEqual(parse_marker_list(None), {})

    def test_single_part_skipped(self):
        self.assertEqual(parse_marker_list(["onlyalg"]), {})


# ===========================================================================
# load_data_as_dataframe
# ===========================================================================

class TestLoadDataAsDataframe(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.sample = _make_jsonl(self.tmp, SAMPLE_RECORDS)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_loads_correctly(self):
        df = load_data_as_dataframe(self.sample, {})
        self.assertIsNotNone(df)
        self.assertFalse(df.empty)
        self.assertIn("algorithm", df.columns)
        self.assertIn("threads", df.columns)
        self.assertIn("metrics.result", df.columns)

    def test_missing_file_returns_none(self):
        self.assertIsNone(load_data_as_dataframe(os.path.join(self.tmp, "ghost.jsonl"), {}))

    def test_exclude_removes_algorithm(self):
        df = load_data_as_dataframe(self.sample, {"exclude": ["AlgC"]})
        self.assertNotIn("AlgC", df["algorithm"].values)

    def test_exclude_multiple(self):
        df = load_data_as_dataframe(self.sample, {"exclude": ["AlgA", "AlgC"]})
        self.assertEqual(set(df["algorithm"].unique()), {"AlgB"})

    def test_rename_applied(self):
        df = load_data_as_dataframe(self.sample, {"rename": ["AlgA==BetterAlg"]})
        self.assertIn("BetterAlg", df["algorithm"].values)
        self.assertNotIn("AlgA", df["algorithm"].values)

    def test_empty_file_returns_empty_df(self):
        path = os.path.join(self.tmp, "empty.jsonl")
        open(path, "w").close()
        df = load_data_as_dataframe(path, {})
        self.assertIsNotNone(df)
        self.assertTrue(df.empty)

    def test_invalid_json_returns_none(self):
        path = os.path.join(self.tmp, "bad.jsonl")
        with open(path, "w") as f:
            f.write("{bad json}\n")
        self.assertIsNone(load_data_as_dataframe(path, {}))

    def test_blank_lines_ignored(self):
        path = os.path.join(self.tmp, "blanks.jsonl")
        with open(path, "w") as f:
            f.write('{"algorithm":"X","threads":1,"metrics":{"result":1.0}}\n')
            f.write("\n\n")
            f.write('{"algorithm":"Y","threads":2,"metrics":{"result":2.0}}\n')
        df = load_data_as_dataframe(path, {})
        self.assertEqual(len(df), 2)

    def test_no_exclude_keeps_all(self):
        df = load_data_as_dataframe(self.sample, {})
        self.assertEqual(set(df["algorithm"].unique()), {"AlgA", "AlgB", "AlgC"})

    def test_cache_columns_normalized(self):
        """json_normalize must flatten nested metrics into dotted column names."""
        tmp = tempfile.mkdtemp()
        try:
            path = _make_jsonl(tmp, CACHE_RECORDS)
            df = load_data_as_dataframe(path, {})
            for col in [
                "metrics.L1_dcache_load_misses",
                "metrics.l2_lines_in_all",
                "metrics.LLC_load_misses",
                "metrics.total_inserts",
                "metrics.total_deletes",
            ]:
                self.assertIn(col, df.columns, f"Missing column: {col}")
        finally:
            shutil.rmtree(tmp)


# ===========================================================================
# per-plot exclude (bash/config.json)
# ===========================================================================

class TestPlotExclude(unittest.TestCase):

    def test_resolve_exclude_maps_raw_to_display(self):
        rename = {"FRX_L": "Twin-List", "Bst": "BST"}
        names = resolve_exclude_names(["FRX_L", "BST"], rename)
        self.assertIn("FRX_L", names)
        self.assertIn("Twin-List", names)
        self.assertIn("BST", names)

    def test_apply_plot_exclude_by_display_name(self):
        df = pd.DataFrame({
            "algorithm": ["Twin-List", "Twin-LJ", "BST"],
            "threads": [1, 1, 1],
        })
        out = apply_plot_exclude(df, {"exclude": ["Twin-List"]}, {"FRX_L": "Twin-List"})
        self.assertEqual(set(out["algorithm"]), {"Twin-LJ", "BST"})

    def test_apply_plot_exclude_by_raw_name(self):
        df = pd.DataFrame({
            "algorithm": ["Twin-List", "Twin-LJ", "BST"],
            "threads": [1, 1, 1],
        })
        out = apply_plot_exclude(df, {"exclude": ["FRX_L"]}, {"FRX_L": "Twin-List"})
        self.assertEqual(set(out["algorithm"]), {"Twin-LJ", "BST"})

    def test_apply_plot_exclude_noop_when_missing(self):
        df = pd.DataFrame({"algorithm": ["A"], "threads": [1]})
        out = apply_plot_exclude(df, {}, {})
        self.assertEqual(len(out), 1)


# ===========================================================================
# get_unique_base_dir
# ===========================================================================

class TestGetUniqueBaseDir(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_base_when_not_exists(self):
        self.assertEqual(get_unique_base_dir(self.tmp, "res"), os.path.join(self.tmp, "res"))

    def test_increments_when_exists(self):
        os.makedirs(os.path.join(self.tmp, "res"))
        self.assertEqual(get_unique_base_dir(self.tmp, "res"), os.path.join(self.tmp, "res_1"))

    def test_increments_twice(self):
        os.makedirs(os.path.join(self.tmp, "res"))
        os.makedirs(os.path.join(self.tmp, "res_1"))
        self.assertEqual(get_unique_base_dir(self.tmp, "res"), os.path.join(self.tmp, "res_2"))

    def test_custom_folder_name(self):
        self.assertTrue(get_unique_base_dir(self.tmp, "output").endswith("output"))


# ===========================================================================
# ensure_subdirectories
# ===========================================================================

class TestEnsureSubdirectories(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_creates_nested_path(self):
        path = ensure_subdirectories(self.tmp, "filter1", "stats", "mean")
        self.assertTrue(os.path.isdir(path))
        self.assertTrue(path.endswith(os.path.join("filter1", "stats", "mean")))

    def test_idempotent(self):
        p1 = ensure_subdirectories(self.tmp, "f", "stats", "mean")
        p2 = ensure_subdirectories(self.tmp, "f", "stats", "mean")
        self.assertEqual(p1, p2)

    def test_none_filter_defaults_to_all(self):
        self.assertIn("all", ensure_subdirectories(self.tmp, None, "stats", "mean"))

    def test_uppercase_normalized(self):
        path = ensure_subdirectories(self.tmp, "f", "STATS", "MEAN")
        self.assertIn("stats", path)
        self.assertIn("mean", path)

    def test_created_dir_is_writable(self):
        path = ensure_subdirectories(self.tmp, "myfilter", "stats", "mean")
        f = os.path.join(path, "test.txt")
        with open(f, "w") as fh:
            fh.write("ok")
        self.assertTrue(os.path.exists(f))


# ===========================================================================
# calculate_x_positions
# ===========================================================================

class TestCalculateXPositions(unittest.TestCase):

    def test_single_value(self):
        mapping, _, labels = calculate_x_positions([4], method="linear")
        self.assertAlmostEqual(mapping[4], 0.0)
        self.assertEqual(labels, ["4"])

    def test_two_values_linear(self):
        mapping, _, _ = calculate_x_positions([1, 5], method="linear")
        self.assertAlmostEqual(mapping[1], 0.0)
        self.assertAlmostEqual(mapping[5], 5.0)

    def test_deduplicates(self):
        mapping, _, _ = calculate_x_positions([2, 2, 4], method="linear")
        self.assertEqual(len(mapping), 2)

    def test_log_monotone(self):
        mapping, _, _ = calculate_x_positions([1, 2, 4, 8], method="log")
        vals = [mapping[x] for x in [1, 2, 4, 8]]
        self.assertEqual(vals, sorted(vals))

    def test_log_handles_zero(self):
        mapping, _, _ = calculate_x_positions([0, 1, 2], method="log")
        self.assertIn(0, mapping)

    def test_empty_input(self):
        mapping, positions, labels = calculate_x_positions([])
        self.assertEqual(mapping, {})
        self.assertEqual(positions, [])
        self.assertEqual(labels, [])

    def test_labels_are_strings(self):
        _, _, labels = calculate_x_positions([1, 4, 16])
        for lbl in labels:
            self.assertIsInstance(lbl, str)

    def test_linear_range_0_to_5(self):
        mapping, _, _ = calculate_x_positions([10, 20, 30, 40, 50], method="linear")
        vals = list(mapping.values())
        self.assertAlmostEqual(min(vals), 0.0)
        self.assertAlmostEqual(max(vals), 5.0)

    def test_returns_list_for_positions(self):
        _, positions, _ = calculate_x_positions([1, 2, 3])
        self.assertIsInstance(positions, list)


# ===========================================================================
# aggregate_metric - basic
# ===========================================================================

class TestAggregateMetric(unittest.TestCase):

    def _df(self, results):
        return pd.DataFrame({"metrics.result": results, "threads": [1] * len(results)})

    def _def(self, agg="mean", k=0, y="metrics.result"):
        return {"data_source": {"y_value": y, "aggregator": agg, "trim_by": "metrics.result"}, "trim_k": k}

    def test_mean_basic(self):
        self.assertAlmostEqual(aggregate_metric(self._df([10.0, 20.0, 30.0]), self._def()), 20.0)

    def test_mean_single(self):
        self.assertAlmostEqual(aggregate_metric(self._df([42.0]), self._def()), 42.0)

    def test_trim_removes_extremes(self):
        result = aggregate_metric(self._df([1.0, 10.0, 10.0, 10.0, 100.0]), self._def("mean", k=1))
        self.assertAlmostEqual(result, 10.0)

    def test_trim_k_too_large_uses_all(self):
        self.assertAlmostEqual(aggregate_metric(self._df([1.0, 2.0]), self._def("mean", k=2)), 1.5)

    def test_trim_column_missing_warns(self):
        import io
        df = pd.DataFrame({"metrics.result": [5.0, 5.0], "threads": [1, 1]})
        plot_def = {"data_source": {"y_value": "metrics.result", "aggregator": "mean", "trim_by": "nonexistent"}, "trim_k": 0}
        buf = io.StringIO()
        with unittest.mock.patch("sys.stdout", buf):
            result = aggregate_metric(df, plot_def)
        self.assertAlmostEqual(result, 5.0)
        self.assertIn("WARN", buf.getvalue())

    def test_invalid_expression_returns_nan(self):
        plot_def = {"data_source": {"y_value": "expression: completely_missing / 0", "aggregator": "mean", "trim_by": "metrics.result"}, "trim_k": 0}
        self.assertTrue(math.isnan(aggregate_metric(self._df([1.0, 2.0]), plot_def)))

    def test_unknown_agg_fallback(self):
        self.assertAlmostEqual(aggregate_metric(self._df([10.0, 20.0]), self._def("unknown")), 15.0)

    def test_empty_group_returns_nan(self):
        df = pd.DataFrame({"metrics.result": pd.Series([], dtype=float), "threads": []})
        self.assertTrue(math.isnan(aggregate_metric(df, self._def())))


# ===========================================================================
# aggregate_metric - cache-miss division expression
# ===========================================================================

class TestCacheMissExpression(unittest.TestCase):
    """
    Tests for the production hardware-counter expression:

        (L1_dcache_load_misses + l2_lines_in_all + LLC_load_misses)
        -------------------------------------------------------------
                    total_inserts + total_deletes
    """

    def _make_df(self, rows):
        """Build a flat DataFrame from a list of metric dicts (already flattened)."""
        return pd.DataFrame(rows)

    def _row(self, l1, l2, llc, ins, dels, alg="AlgA", threads=1):
        return {
            "algorithm": alg,
            "threads": threads,
            "metrics.L1_dcache_load_misses": float(l1),
            "metrics.l2_lines_in_all":       float(l2),
            "metrics.LLC_load_misses":        float(llc),
            "metrics.total_inserts":          float(ins),
            "metrics.total_deletes":          float(dels),
            "metrics.result":                 1.0,
        }

    # ------------------------------------------------------------------
    # Correctness
    # ------------------------------------------------------------------

    def test_single_row_correct_value(self):
        """(1000+500+200)/(800+200) = 1700/1000 = 1.7"""
        df = self._make_df([self._row(1000, 500, 200, 800, 200)])
        result = aggregate_metric(df, _cache_plot_def())
        self.assertAlmostEqual(result, 1.7, places=6)

    def test_two_equal_rows_mean_unchanged(self):
        """Both rows give 1.7, so mean is 1.7."""
        df = self._make_df([
            self._row(1000, 500, 200, 800, 200),
            self._row(1000, 500, 200, 800, 200),
        ])
        self.assertAlmostEqual(aggregate_metric(df, _cache_plot_def()), 1.7, places=6)

    def test_two_different_rows_mean(self):
        """
        Row 1: (1000+500+200)/(800+200) = 1.7
        Row 2: (2000+1000+400)/(1600+400) = 3400/2000 = 1.7
        mean = 1.7
        """
        df = self._make_df([
            self._row(1000, 500, 200, 800, 200),
            self._row(2000, 1000, 400, 1600, 400),
        ])
        self.assertAlmostEqual(aggregate_metric(df, _cache_plot_def()), 1.7, places=6)

    def test_different_ratios_mean(self):
        """
        Row 1: (100+50+25)/(200+50)   = 175/250  = 0.7
        Row 2: (400+200+100)/(400+100) = 700/500  = 1.4
        mean = (0.7 + 1.4) / 2 = 1.05
        """
        df = self._make_df([
            self._row(100, 50, 25, 200, 50),
            self._row(400, 200, 100, 400, 100),
        ])
        self.assertAlmostEqual(aggregate_metric(df, _cache_plot_def()), 1.05, places=6)

    def test_result_is_positive_for_valid_data(self):
        df = self._make_df([self._row(500, 250, 100, 400, 100)])
        result = aggregate_metric(df, _cache_plot_def())
        self.assertGreater(result, 0)
        self.assertFalse(math.isnan(result))

    # ------------------------------------------------------------------
    # Edge cases: zero denominator
    # ------------------------------------------------------------------

    def test_zero_denominator_replaced_by_zero(self):
        """
        When total_inserts=0 and total_deletes=0 the division produces inf/nan.
        aggregate_metric must replace it with 0 and not crash.
        """
        df = self._make_df([self._row(100, 50, 25, 0, 0)])
        result = aggregate_metric(df, _cache_plot_def())
        # After fillna(0) the result must be a finite number, not NaN
        self.assertFalse(math.isnan(result))
        self.assertTrue(math.isfinite(result))

    def test_zero_denominator_does_not_raise(self):
        df = self._make_df([
            self._row(100, 50, 25, 0, 0),
            self._row(100, 50, 25, 0, 0),
        ])
        try:
            aggregate_metric(df, _cache_plot_def())
        except Exception as e:
            self.fail(f"aggregate_metric raised an exception with zero denominator: {e}")

    def test_mixed_zero_and_valid_denominator(self):
        """
        Row 1 has denominator 0 (-> replaced with 0),
        Row 2 has ratio 1.7. Mean = (0 + 1.7) / 2 = 0.85.
        """
        df = self._make_df([
            self._row(100, 50, 25, 0, 0),           # denominator = 0 -> value = 0
            self._row(1000, 500, 200, 800, 200),     # ratio = 1.7
        ])
        result = aggregate_metric(df, _cache_plot_def())
        self.assertAlmostEqual(result, 0.85, places=6)

    # ------------------------------------------------------------------
    # Edge cases: zero numerator
    # ------------------------------------------------------------------

    def test_zero_numerator_gives_zero(self):
        """All cache counters = 0 -> ratio = 0/denominator = 0.0."""
        df = self._make_df([self._row(0, 0, 0, 500, 500)])
        result = aggregate_metric(df, _cache_plot_def())
        self.assertAlmostEqual(result, 0.0, places=6)

    # ------------------------------------------------------------------
    # Trim interacting with expression
    # ------------------------------------------------------------------

    def test_trim_on_expression_result(self):
        """
        5 rows with ratios: 0.1, 1.7, 1.7, 1.7, 99.0
        trim_k=1 removes the min (0.1) and max (99.0) after sorting by
        trim_by ('metrics.result'), leaving the three 1.7 rows.
        All three rows have identical metrics so the ratio stays 1.7.
        """
        df = self._make_df([
            self._row(10,   5,   2,   100, 0),   # ratio ~ 0.17  (lowest metrics.result below)
            self._row(1000, 500, 200, 800, 200),  # ratio = 1.7
            self._row(1000, 500, 200, 800, 200),  # ratio = 1.7
            self._row(1000, 500, 200, 800, 200),  # ratio = 1.7
            self._row(9000, 4500, 1800, 7200, 1800), # ratio = 1.7 (highest metrics.result)
        ])
        # Assign different metrics.result values so trim_by sorts them correctly
        df["metrics.result"] = [0.1, 1.0, 1.0, 1.0, 99.0]
        result = aggregate_metric(df, _cache_plot_def("mean", k=1))
        self.assertAlmostEqual(result, 1.7, places=5)

    # ------------------------------------------------------------------
    # Loading from JSONL and then evaluating the expression
    # ------------------------------------------------------------------

    def test_expression_on_loaded_dataframe(self):
        """End-to-end: load JSONL -> group by algorithm -> evaluate expression."""
        tmp = tempfile.mkdtemp()
        try:
            path = _make_jsonl(tmp, CACHE_RECORDS)
            df = load_data_as_dataframe(path, {"exclude": ["AlgZero", "AlgZeroNum"]})
            self.assertIsNotNone(df)

            alg_a = df[df["algorithm"] == "AlgA"]
            # Use only threads=1 rows for this check
            alg_a_t1 = alg_a[alg_a["threads"] == 1]
            # Row1: (1000+500+200)/(800+200)   = 1.7
            # Row2: (2000+1000+400)/(1600+400) = 1.7
            result = aggregate_metric(alg_a_t1, _cache_plot_def())
            self.assertAlmostEqual(result, 1.7, places=5)
        finally:
            shutil.rmtree(tmp)

    def test_expression_alg_b_on_loaded_dataframe(self):
        """AlgB: (500+250+100)/(400+100) = 850/500 = 1.7."""
        tmp = tempfile.mkdtemp()
        try:
            path = _make_jsonl(tmp, CACHE_RECORDS)
            df = load_data_as_dataframe(path, {})
            alg_b = df[(df["algorithm"] == "AlgB") & (df["threads"] == 1)]
            result = aggregate_metric(alg_b, _cache_plot_def())
            self.assertAlmostEqual(result, 1.7, places=5)
        finally:
            shutil.rmtree(tmp)

    def test_algzero_on_loaded_dataframe_is_finite(self):
        """AlgZero has denominator 0 -> result must be finite (not NaN/inf)."""
        tmp = tempfile.mkdtemp()
        try:
            path = _make_jsonl(tmp, CACHE_RECORDS)
            df = load_data_as_dataframe(path, {})
            alg_zero = df[df["algorithm"] == "AlgZero"]
            result = aggregate_metric(alg_zero, _cache_plot_def())
            self.assertFalse(math.isnan(result))
            self.assertTrue(math.isfinite(result))
        finally:
            shutil.rmtree(tmp)

    def test_algzeronumerator_on_loaded_dataframe_is_zero(self):
        """AlgZeroNum has numerator 0 and valid denominator -> ratio = 0."""
        tmp = tempfile.mkdtemp()
        try:
            path = _make_jsonl(tmp, CACHE_RECORDS)
            df = load_data_as_dataframe(path, {})
            alg_zn = df[df["algorithm"] == "AlgZeroNum"]
            result = aggregate_metric(alg_zn, _cache_plot_def())
            self.assertAlmostEqual(result, 0.0, places=6)
        finally:
            shutil.rmtree(tmp)


# ===========================================================================
# Integration
# ===========================================================================

class TestIntegration(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.sample = _make_jsonl(self.tmp, SAMPLE_RECORDS)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_load_exclude_then_aggregate(self):
        df = load_data_as_dataframe(self.sample, {"exclude": ["AlgC"]})
        self.assertIsNotNone(df)
        filtered = df.query("threads > 1")
        self.assertEqual(len(filtered), 2)
        for _, group in filtered.groupby("algorithm"):
            r = aggregate_metric(group, BASE_PLOT_DEF)
            self.assertFalse(math.isnan(r))
            self.assertGreater(r, 0)

    def test_x_positions_cover_all_threads(self):
        df = load_data_as_dataframe(self.sample, {})
        thread_vals = sorted(df["threads"].unique().tolist())
        mapping, positions, labels = calculate_x_positions(thread_vals, method="linear")
        self.assertEqual(set(mapping.keys()), set(thread_vals))
        self.assertEqual(len(positions), len(thread_vals))

    def test_subdir_then_write(self):
        path = ensure_subdirectories(self.tmp, "myfilter", "stats", "mean")
        f = os.path.join(path, "dummy.txt")
        with open(f, "w") as fh:
            fh.write("ok")
        self.assertTrue(os.path.exists(f))

    def test_unique_dir_no_collision(self):
        r1 = get_unique_base_dir(self.tmp, "res")
        os.makedirs(r1)
        r2 = get_unique_base_dir(self.tmp, "res")
        self.assertNotEqual(r1, r2)

    def test_cache_expression_multi_thread(self):
        """
        Load CACHE_RECORDS, exclude zero-edge-case algs, then check that
        AlgA at threads=2 evaluates the cache expression correctly.
        (3000+1500+600)/(2400+600) = 5100/3000 = 1.7
        """
        tmp = tempfile.mkdtemp()
        try:
            path = _make_jsonl(tmp, CACHE_RECORDS)
            df = load_data_as_dataframe(path, {"exclude": ["AlgZero", "AlgZeroNum"]})
            alg_a_t2 = df[(df["algorithm"] == "AlgA") & (df["threads"] == 2)]
            result = aggregate_metric(alg_a_t2, _cache_plot_def())
            self.assertAlmostEqual(result, 1.7, places=5)
        finally:
            shutil.rmtree(tmp)


# ===========================================================================
# Entry point
# ===========================================================================

if __name__ == "__main__":
    loader = unittest.TestLoader()
    suite  = loader.loadTestsFromModule(sys.modules[__name__])
    result = ColorRunner().run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)