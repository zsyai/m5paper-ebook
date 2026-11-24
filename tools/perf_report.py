#!/usr/bin/env python3
"""Summarizes and compares [PERF] / [BOOT] logs from an m5paper_perf build.

Usage:
  pio device monitor -e m5paper_perf -p <port>        # log2file -> logs/*.log
  python3 tools/perf_report.py logs/device-monitor-<timestamp>.log [...]
      Prints scopes aggregated by name (count / total / max ms), plus marks
      and values in log order.
  python3 tools/perf_report.py --json out.json <logs...>
      Also writes the key metrics (below) as JSON.
  python3 tools/perf_report.py --compare perf/baseline.json <logs...>
      Compares the key metrics against a baseline; exits 1 if any is slower
      than the threshold or memory dropped (see THRESHOLDS).

Key metrics are taken from the log's first occurrence of each mark / scope
(boot) and the median of repeated scopes (page turns), so capture logs the
same way each time: power-on reset, open the same book, turn ~10 pages.
"""

import argparse
import collections
import json
import re
import statistics
import sys

_SCOPE_RE = re.compile(r"\[PERF\] (\S+)\s+(\s*)(\S.*?)  (\d+)\.(\d{3}) ms$")
_MARK_RE = re.compile(r"\[PERF\] (\S+)\s+@(\d+\.\d{3}) ms (\S+) (.*)$")
_VALUE_RE = re.compile(r"\[PERF\] (\S+)\s+(\S.*?) = (-?\d+)$")
_HEAP_RE = re.compile(r"heap=(\d+) psram=(\d+)")
_BOOT_RE = re.compile(r"\[BOOT\] (\S+) early_app=(\d+) us now=(\d+) us")

# metric name -> (how to extract, "lower" or "higher" is better, threshold)
# Thresholds: fraction of the baseline a metric may get worse by.
THRESHOLDS = {
    "boot_ready_ms": 0.10,
    "boot_early_app_ms": 0.10,
    "m5_begin_ms": 0.10,
    "open_book_ms": 0.10,
    "page_turn_median_ms": 0.10,
    "page_turn_max_ms": 0.20,
    "draw_reading_view_median_ms": 0.10,
    "free_heap_at_boot": 0.05,
    "free_psram_at_boot": 0.05,
}
HIGHER_IS_BETTER = {"free_heap_at_boot", "free_psram_at_boot"}


def parse(paths):
  """Returns (totals, events, samples, metrics)."""
  totals = collections.OrderedDict()
  events = []
  samples = collections.defaultdict(list)  # scope name -> [ms]
  metrics = {"invariant_errors": 0}
  turn_heaps = []
  turn_psrams = []
  for path in paths:
    with open(path, encoding="utf-8", errors="replace") as f:
      for raw in f:
        line = raw.rstrip("\r\n")
        if "[INVARIANT]" in line:
          metrics["invariant_errors"] += 1
          events.append(f"  INVARIANT  {line[line.index('[INVARIANT]'):]}")
        b = _BOOT_RE.search(line)
        if (
            b
            and b.group(1) == "setup_start"
            and "boot_early_app_ms" not in metrics
        ):
          early = int(b.group(2))
          # Only meaningful after a power-on reset (RTC time starts at 0).
          if early < 10_000_000:
            metrics["boot_early_app_ms"] = early / 1000.0
        if "[PERF]" not in line:
          continue
        line = line[line.index("[PERF]") :]
        m = _MARK_RE.match(line)
        if m:
          at_ms = float(m.group(2))
          name = m.group(3)
          events.append(f"@{m.group(2):>10} ms  {name}  {m.group(4)}")
          h = _HEAP_RE.search(m.group(4))
          if name == "setup.boot_ready" and "boot_ready_ms" not in metrics:
            metrics["boot_ready_ms"] = at_ms
            if h:
              metrics["free_heap_at_boot"] = int(h.group(1))
              metrics["free_psram_at_boot"] = int(h.group(2))
          elif name == "turn_mem_check" and h:
            turn_heaps.append(int(h.group(1)))
            turn_psrams.append(int(h.group(2)))
          continue
        m = _SCOPE_RE.match(line)
        if m:
          ms = int(m.group(4)) + int(m.group(5)) / 1000.0
          key = (m.group(1), m.group(3))
          count, total, peak = totals.get(key, (0, 0.0, 0.0))
          totals[key] = (count + 1, total + ms, max(peak, ms))
          samples[m.group(3)].append(ms)
          continue
        m = _VALUE_RE.match(line)
        if m:
          events.append(f"{'':>14}  {m.group(2)} = {m.group(3)}")

  if samples.get("M5.begin"):
    metrics["m5_begin_ms"] = samples["M5.begin"][0]
  open_samples = (
      samples.get("BookSession::Open")
      or samples.get("UIManager::openBook")
      or []
  )
  if open_samples:
    metrics["open_book_ms"] = open_samples[0]
  turns = (
      samples.get("BookSession::NextPage", [])
      + samples.get("BookSession::PrevPage", [])
      + samples.get("UIManager::nextPage", [])
      + samples.get("UIManager::prevPage", [])
  )
  if turns:
    metrics["page_turn_median_ms"] = statistics.median(turns)
    metrics["page_turn_max_ms"] = max(turns)
  draws = samples.get("BookSession::DrawReadingView", []) + samples.get(
      "UIManager::drawReadingView", []
  )
  if draws:
    metrics["draw_reading_view_median_ms"] = statistics.median(draws)
  if turn_heaps:
    metrics["min_heap_at_turn_check"] = min(turn_heaps)
  if turn_psrams:
    metrics["min_psram_at_turn_check"] = min(turn_psrams)
  return totals, events, samples, metrics


def print_report(totals, events):
  print("== Marks / values (log order) ==")
  for e in events:
    print(e)
  print()
  print(
      f"== Scopes ==\n{'task':<14}{'scope':<34}{'n':>5}"
      f"{'total ms':>12}{'max ms':>10}"
  )
  for (task, name), (count, total, peak) in totals.items():
    print(f"{task:<14}{name:<34}{count:>5}{total:>12.1f}{peak:>10.1f}")


def compare(metrics, baseline):
  """Prints a table and returns the number of regressions."""
  regressions = 0
  print(
      f"\n== Compare ==\n{'metric':<30}{'baseline':>14}{'current':>14}"
      f"{'change':>10}  verdict"
  )
  for name, limit in THRESHOLDS.items():
    base = baseline.get(name)
    cur = metrics.get(name)
    if base is None or cur is None:
      verdict = "n/a" if base is None else "MISSING"
      if base is not None:
        regressions += 1
      fmt = lambda v: "-" if v is None else f"{v:.1f}"
      print(f"{name:<30}{fmt(base):>14}{fmt(cur):>14}{'':>10}  {verdict}")
      continue
    if base == 0:
      change = 0.0
    else:
      change = (cur - base) / base
    worse = change < -limit if name in HIGHER_IS_BETTER else change > limit
    better = change > limit if name in HIGHER_IS_BETTER else change < -limit
    verdict = "REGRESSION" if worse else ("improved" if better else "ok")
    if worse:
      regressions += 1
    print(
        f"{name:<30}{base:>14.1f}{cur:>14.1f}{change * 100:>+9.1f}%  {verdict}"
    )
  inv = metrics.get("invariant_errors", 0)
  if inv > 0:
    regressions += 1
    print(f"{'invariant_errors':<30}{0:>14}{inv:>14}           REGRESSION")
  for boot_key, turn_key in (
      ("free_heap_at_boot", "min_heap_at_turn_check"),
      ("free_psram_at_boot", "min_psram_at_turn_check"),
  ):
    if boot_key in metrics and turn_key in metrics and metrics[boot_key] > 0:
      drop = (metrics[turn_key] - metrics[boot_key]) / metrics[boot_key]
      verdict = "REGRESSION" if drop < -0.05 else "ok"
      if drop < -0.05:
        regressions += 1
      print(
          f"{turn_key:<30}{metrics[boot_key]:>14.1f}{metrics[turn_key]:>14.1f}"
          f"{drop * 100:>+9.1f}%  {verdict}"
      )
  return regressions


def main(argv):
  ap = argparse.ArgumentParser(
      description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
  )
  ap.add_argument("logs", nargs="+")
  ap.add_argument("--json", help="write key metrics to this file")
  ap.add_argument("--compare", help="baseline JSON to compare against")
  ap.add_argument("--quiet", action="store_true", help="skip the full report")
  args = ap.parse_args(argv[1:])

  totals, events, _, metrics = parse(args.logs)
  if not args.quiet:
    print_report(totals, events)
  print("\n== Key metrics ==")
  for k, v in metrics.items():
    print(f"{k:<30}{v:>14.1f}")
  if args.json:
    with open(args.json, "w") as f:
      json.dump(metrics, f, indent=2, sort_keys=True)
      f.write("\n")
    print(f"\nwrote {args.json}")
  if args.compare:
    with open(args.compare) as f:
      baseline = json.load(f)
    regressions = compare(metrics, baseline)
    if regressions:
      print(f"\n{regressions} regression(s) against {args.compare}")
      return 1
    print(f"\nno regressions against {args.compare}")
  return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv))
